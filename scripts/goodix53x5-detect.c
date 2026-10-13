/* Identify Goodix devices that may be compatible with the 53x5 Milan driver,
 * over either USB framing it supports. The probe makes no persistent device
 * writes and captures no biometric data. */

#include <gio/gio.h>
#include <gusb.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define GOODIX_VID 0x27c6
#define GOODIX_CHUNK_SIZE 64
/* Wrapped framing: [flags][length LE16][sum of the first three] then the
 * message; 0xa0 marks a protocol message. */
#define GOODIX_PACK_HEADER 4
#define GOODIX_PACK_MESSAGE 0xa0
#define GOODIX_PACK_FAMILY_MESSAGE 0xa
#define GOODIX_PACK_FAMILY_TLS 0xb
#define GOODIX_PSK_HASH_DIRECT 0xB003
#define GOODIX_PSK_HASH_WRAPPED 0xbb020001
#define GOODIX_PSK_LEN 32
/* smell: the path and parse_psk_file mirror the driver's goodix_load_psk
 * (device/session.c), kept so the detector stays one self-contained file.
 * Fix if the detector ever builds against the driver tree. */
#define GOODIX_PSK_STATE_FILE "/var/lib/fprint/goodix53x5.psk"
#define GOODIX_GUIDE "WINDOWS-DUAL-BOOT.md"
#define GOODIX_TIMEOUT_MS 2000
#define GOODIX_MAX_MESSAGE (64 * 1024)
#define MILAN_CHIP_FAMILY_MASK 0xffffff00u
#define MILAN_CHIP_FAMILY_PREFIX 0x00220c00u
#define SYMBOL_CHECK "\xe2\x9c\x93"
#define SYMBOL_CROSS "\xe2\x9c\x97"
#define SYMBOL_WARN  "\xe2\x9a\xa0"

typedef enum
{
  PROBE_COMPATIBLE,
  PROBE_INCOMPATIBLE,
  PROBE_INCONCLUSIVE,
  /* Supported, but the driver can't open it until the reason's steps are done. */
  PROBE_NEEDS_SETUP,
} ProbeResult;

/* What a supported sensor needs before the driver can open it; each has its
 * own instructions for the user. */
typedef enum
{
  /* Windows' driver left it in the bootloader with a new key. */
  SETUP_FINISH_WINDOWS_UPDATE,
  /* It holds Windows' key, and none is imported. */
  SETUP_IMPORT_KEY,
  /* Windows has set a new key since the import. */
  SETUP_REIMPORT_KEY,
  /* It was reset to the all-zero key, and an import is left behind. */
  SETUP_REMOVE_STALE_KEY,
} SetupStep;

/* A GTLS pre-shared key, typed apart from the SHA-256 hashes the sensor
 * reports of one. */
typedef struct
{
  guint8 bytes[GOODIX_PSK_LEN];
} Psk;

/* The key the driver opens the sensor with. */
typedef struct
{
  Psk      key;
  /* True if it comes from GOODIX_PSK_STATE_FILE; false for the all-zero
   * default, which the advice for a mismatch depends on. */
  gboolean imported;
} DriverPsk;

typedef struct _Probe Probe;

/* What differs between the sensor families the driver runs: how each is
 * reached over USB and the commands it answers differently. 533c puts the
 * same chip behind a GM168SEC secure element that speaks the wrapped framing. */
typedef struct
{
  const gchar  *name;
  /* Checks whether the layout is tried on product @pid: other Goodix sensors
   * share the wrapped layout's endpoints but not its protocol. */
  gboolean    (*probes_pid) (guint16 pid);
  guint8        interface;
  guint8        ep_out;
  guint8        ep_in;
  /* Writes @message into the one USB chunk sent for it. */
  gboolean    (*frame) (const GByteArray *message,
                        guint8           *transfer,
                        GError          **error);
  /* Returns the next protocol message from the sensor. */
  GByteArray *(*receive) (Probe   *probe,
                          GError **error);
  /* Bytes of the ping's zero payload. */
  gsize         ping_payload_length;
  guint8        reset_payload[2];
  /* TRUE where the sensor answers the reset, so its reply is read with it. */
  gboolean      reset_replies;
  /* The slot the PSK hash is read from and the record type it comes back as. */
  guint32       psk_hash_type;
  /* Writes the request reading @type into @request; returns its length. */
  gsize       (*psk_hash_request) (guint32 type,
                                   guint8  request[16]);
  /* TRUE where the driver never provisions the sensor, so it must already hold
   * the key the driver uses: the imported one, or else the all-zero PSK. */
  gboolean      requires_driver_psk;
  /* Part of the firmware version while the sensor runs its bootloader, where
   * the rest of the probe gets no replies; NULL where unknown. */
  const gchar  *bootloader_mark;
} ProbeLayout;

/* The device being probed and how to reach it. */
struct _Probe
{
  GUsbDevice        *device;
  const ProbeLayout *layout;
  /* Bytes read from the sensor and not yet taken as a message. */
  GByteArray        *stream;
};

typedef struct
{
  guint16            vid;
  guint16            pid;
  const ProbeLayout *layout;
  guint32            chip_id;
  gchar             *firmware;
  ProbeResult        result;
  gchar             *reason;
} ProbeReport;

static gboolean use_color;

static const gchar *
color (const gchar *ansi)
{
  return use_color ? ansi : "";
}

static void
print_result (const gchar *ansi,
              const gchar *symbol,
              const gchar *text)
{
  g_print ("\n%s", color (ansi));
  fputs (symbol, stdout);
  g_print (" %s%s\n", text, color ("\033[0m"));
}

static guint8
checksum_for (const guint8 *data,
              gsize         length)
{
  guint sum = 0;

  for (gsize i = 0; i < length; i++)
    sum += data[i];
  return (0xAA - sum) & 0xFF;
}

static GByteArray *
build_message (guint8        category,
               guint8        command,
               const guint8 *payload,
               gsize         payload_length)
{
  GByteArray *message = g_byte_array_sized_new (payload_length + 4);
  guint8 header[3] = {
    (category << 4) | (command << 1),
    (payload_length + 1) & 0xFF,
    ((payload_length + 1) >> 8) & 0xFF,
  };
  guint8 checksum;

  g_byte_array_append (message, header, sizeof (header));
  g_byte_array_append (message, payload, payload_length);
  checksum = checksum_for (message->data, message->len);
  g_byte_array_append (message, &checksum, 1);
  return message;
}

static gboolean
validate_message (const guint8 *message,
                  gsize         length,
                  GError      **error)
{
  gsize expected;
  guint8 received_checksum;

  if (length < 4)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Goodix message is shorter than its header");
      return FALSE;
    }

  expected = 3 + message[1] + ((gsize) message[2] << 8);
  if (expected != length)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Goodix message length mismatch: expected %zu, got %zu",
                   expected, length);
      return FALSE;
    }

  received_checksum = message[length - 1];
  if (received_checksum != 0x88 &&
      received_checksum != checksum_for (message, length - 1))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Goodix message checksum mismatch");
      return FALSE;
    }

  return TRUE;
}

static gboolean
bulk_read (Probe   *probe,
           guint8  *chunk,
           gsize   *actual_length,
           guint    timeout,
           GError **error)
{
  return g_usb_device_bulk_transfer (probe->device, probe->layout->ep_in, chunk,
                                     GOODIX_CHUNK_SIZE, actual_length,
                                     timeout, NULL, error);
}

static guint8
pack_checksum (const guint8 *header)
{
  return header[0] + header[1] + header[2];
}

static void
write_pack_header (guint8 *header,
                   guint8  flags,
                   gsize   length)
{
  header[0] = flags;
  header[1] = length & 0xFF;
  header[2] = (length >> 8) & 0xFF;
  header[3] = pack_checksum (header);
}

/* Returns the payload length a wrapped pack header announces, or FALSE when
 * it is not a header: a family other than 0xa or 0xb, or a wrong checksum. */
static gboolean
read_pack_header (const guint8 *header,
                  gsize        *length)
{
  if ((header[0] >> 4 != GOODIX_PACK_FAMILY_MESSAGE &&
       header[0] >> 4 != GOODIX_PACK_FAMILY_TLS) ||
      pack_checksum (header) != header[3])
    return FALSE;
  *length = header[1] | ((gsize) header[2] << 8);
  return TRUE;
}

static gboolean
receive_made_progress (gsize     appended,
                       guint    *no_progress_count,
                       GError  **error)
{
  if (appended != 0)
    {
      *no_progress_count = 0;
      return TRUE;
    }
  if (++*no_progress_count <= 8)
    return TRUE;

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                       "Goodix reply made no progress");
  return FALSE;
}

static gboolean
drain_input (Probe   *probe,
             GError **error)
{
  guint8 chunk[GOODIX_CHUNK_SIZE];

  for (guint i = 0; i < 64; i++)
    {
      gsize actual_length = 0;
      g_autoptr(GError) read_error = NULL;

      if (bulk_read (probe, chunk, &actual_length, 50, &read_error))
        continue;
      if (g_error_matches (read_error, G_USB_DEVICE_ERROR,
                           G_USB_DEVICE_ERROR_TIMED_OUT))
        return TRUE;

      g_propagate_error (error, g_steal_pointer (&read_error));
      return FALSE;
    }

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                       "Goodix input did not become idle");
  return FALSE;
}

/* Takes the message of the first pack off @stream into @message, leaving
 * the bytes after it. Returns G_IO_STATUS_AGAIN while the pack is
 * incomplete, and fails on a header that is not one or a pack that is not
 * a message. */
static GIOStatus
take_wrapped_message (GByteArray  *stream,
                      GByteArray **message,
                      GError     **error)
{
  gsize padding = 0;
  gsize length;

  /* Packs are zero-padded to whole USB chunks and never start with zero. */
  while (padding < stream->len && stream->data[padding] == 0)
    padding++;
  g_byte_array_remove_range (stream, 0, padding);
  if (stream->len < GOODIX_PACK_HEADER)
    return G_IO_STATUS_AGAIN;
  if (!read_pack_header (stream->data, &length))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Invalid Goodix pack header");
      return G_IO_STATUS_ERROR;
    }
  if (length > GOODIX_MAX_MESSAGE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Invalid Goodix pack size: %zu", length);
      return G_IO_STATUS_ERROR;
    }
  if (stream->len < GOODIX_PACK_HEADER + length)
    return G_IO_STATUS_AGAIN;
  if (stream->data[0] != GOODIX_PACK_MESSAGE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Unexpected Goodix pack 0x%02x", stream->data[0]);
      return G_IO_STATUS_ERROR;
    }
  *message = g_byte_array_sized_new (length);
  g_byte_array_append (*message, stream->data + GOODIX_PACK_HEADER, length);
  g_byte_array_remove_range (stream, 0, GOODIX_PACK_HEADER + length);
  return G_IO_STATUS_NORMAL;
}

/* Returns the protocol message carried by the next wrapped pack. */
static GByteArray *
receive_wrapped_message (Probe   *probe,
                         GError **error)
{
  g_autoptr(GByteArray) message = NULL;
  guint8 chunk[GOODIX_CHUNK_SIZE];
  guint no_progress_count = 0;

  for (;;)
    {
      gsize actual_length = 0;
      gsize skip = 0;
      GIOStatus status = take_wrapped_message (probe->stream, &message, error);

      if (status == G_IO_STATUS_ERROR)
        return NULL;
      if (status == G_IO_STATUS_NORMAL)
        break;
      if (!bulk_read (probe, chunk, &actual_length, GOODIX_TIMEOUT_MS, error))
        return NULL;
      while (probe->stream->len == 0 && skip < actual_length && chunk[skip] == 0)
        skip++;
      if (!receive_made_progress (actual_length - skip, &no_progress_count, error))
        return NULL;
      g_byte_array_append (probe->stream, chunk + skip, actual_length - skip);
    }

  if (!validate_message (message->data, message->len, error))
    return NULL;
  return g_steal_pointer (&message);
}

static GByteArray *
receive_direct_message (Probe   *probe,
                        GError **error)
{
  g_autoptr(GByteArray) message = g_byte_array_new ();
  guint8 chunk[GOODIX_CHUNK_SIZE];
  guint8 command_byte = 0;
  gsize expected = 0;
  guint no_progress_count = 0;

  while (message->len < expected || expected == 0)
    {
      gsize actual_length = 0;
      gsize data_offset = 0;
      gsize append_length;

      if (!bulk_read (probe, chunk, &actual_length, GOODIX_TIMEOUT_MS, error))
        return NULL;
      if (actual_length == 0)
        {
          if (!receive_made_progress (0, &no_progress_count, error))
            return NULL;
          continue;
        }

      if (message->len == 0)
        {
          if (actual_length < 3)
            {
              g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                   "Short first Goodix USB chunk");
              return NULL;
            }
          command_byte = chunk[0];
          expected = 3 + chunk[1] + ((gsize) chunk[2] << 8);
          if (expected < 4 || expected > GOODIX_MAX_MESSAGE)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Invalid Goodix message size: %zu", expected);
              return NULL;
            }
        }
      else
        {
          if ((chunk[0] & 1) == 0 || (chunk[0] & 0xFE) != command_byte)
            {
              g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                   "Invalid Goodix continuation chunk");
              return NULL;
            }
          data_offset = 1;
        }

      append_length = MIN (actual_length - data_offset,
                           expected - message->len);
      if (!receive_made_progress (append_length, &no_progress_count, error))
        return NULL;
      if (append_length == 0)
        continue;
      g_byte_array_append (message, chunk + data_offset, append_length);
    }

  if (!validate_message (message->data, message->len, error))
    return NULL;
  return g_steal_pointer (&message);
}

static gboolean
fits_one_chunk (gsize    length,
                GError **error)
{
  if (length <= GOODIX_CHUNK_SIZE)
    return TRUE;
  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Probe command exceeds one USB chunk");
  return FALSE;
}

static gboolean
frame_direct (const GByteArray *message,
              guint8           *transfer,
              GError          **error)
{
  if (!fits_one_chunk (message->len, error))
    return FALSE;
  memcpy (transfer, message->data, message->len);
  return TRUE;
}

static gboolean
frame_wrapped (const GByteArray *message,
               guint8           *transfer,
               GError          **error)
{
  if (!fits_one_chunk (GOODIX_PACK_HEADER + message->len, error))
    return FALSE;
  write_pack_header (transfer, GOODIX_PACK_MESSAGE, message->len);
  memcpy (transfer + GOODIX_PACK_HEADER, message->data, message->len);
  return TRUE;
}

static void
put_u32_le (guint8  *out,
            guint32  value)
{
  for (guint i = 0; i < 4; i++)
    out[i] = (value >> (8 * i)) & 0xFF;
}

static gsize
production_read_request (guint32 type,
                         guint8  request[16])
{
  put_u32_le (request, type);
  return 4;
}

/* Length, offset, preset slot and a zero word. */
static gsize
preset_read_request (guint32 type,
                     guint8  request[16])
{
  put_u32_le (request, 32);
  put_u32_le (request + 4, 0);
  put_u32_le (request + 8, type);
  put_u32_le (request + 12, 0);
  return 16;
}

static gboolean
probes_any_pid (guint16 pid)
{
  (void) pid;
  return TRUE;
}

static gboolean
probes_533c (guint16 pid)
{
  return pid == 0x533c;
}

static const ProbeLayout probe_layouts[] = {
  {
    .name = "direct framing, interface 1",
    .probes_pid = probes_any_pid,
    .interface = 1,
    .ep_out = 0x03,
    .ep_in = 0x81,
    .frame = frame_direct,
    .receive = receive_direct_message,
    .reset_payload = { 0x01, 0x14 },
    .ping_payload_length = 2,
    .psk_hash_type = GOODIX_PSK_HASH_DIRECT,
    .psk_hash_request = production_read_request,
  },
  {
    .name = "wrapped framing, interface 0 (GM168SEC)",
    .probes_pid = probes_533c,
    .interface = 0,
    .ep_out = 0x01,
    .ep_in = 0x83,
    .frame = frame_wrapped,
    .receive = receive_wrapped_message,
    .ping_payload_length = 4,
    .reset_payload = { 0x05, 0x14 },
    .reset_replies = TRUE,
    .psk_hash_type = GOODIX_PSK_HASH_WRAPPED,
    .psk_hash_request = preset_read_request,
    .requires_driver_psk = TRUE,
    .bootloader_mark = "_IAP_",
  },
};

static gboolean
is_ack_for (GByteArray *message,
            guint8      command_byte)
{
  return (message->data[0] >> 4) == 0x0B &&
         ((message->data[0] & 0x0F) >> 1) == 0 &&
         message->len >= 6 && message->data[3] == command_byte &&
         (message->data[4] & 1) != 0;
}

static GByteArray *
run_command (Probe        *probe,
             guint8        category,
             guint8        command,
             const guint8 *payload,
             gsize         payload_length,
             gboolean      expect_reply,
             GError      **error)
{
  g_autoptr(GByteArray) request = NULL;
  g_autoptr(GByteArray) first = NULL;
  guint8 transfer[GOODIX_CHUNK_SIZE] = { 0 };
  guint8 command_byte;
  gsize actual_length = 0;

  request = build_message (category, command, payload, payload_length);
  if (!probe->layout->frame (request, transfer, error))
    return NULL;
  if (!g_usb_device_bulk_transfer (probe->device, probe->layout->ep_out, transfer,
                                   sizeof (transfer), &actual_length,
                                   GOODIX_TIMEOUT_MS, NULL, error))
    return NULL;
  if (actual_length != sizeof (transfer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Short Goodix USB write: %zu", actual_length);
      return NULL;
    }

  command_byte = (category << 4) | (command << 1);
  first = probe->layout->receive (probe, error);
  if (!first)
    return NULL;
  if (!is_ack_for (first, command_byte))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Invalid ACK for Goodix command 0x%02x", command_byte);
      return NULL;
    }

  if (!expect_reply)
    return g_byte_array_new ();
  return probe->layout->receive (probe, error);
}

static gboolean
parse_reply (GByteArray   *message,
             guint8        category,
             guint8        command,
             const guint8 **payload,
             gsize         *payload_length,
             GError       **error)
{
  if ((message->data[0] >> 4) != category ||
      ((message->data[0] & 0x0F) >> 1) != command)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Unexpected Goodix reply 0x%02x", message->data[0]);
      return FALSE;
    }

  *payload = message->data + 3;
  *payload_length = message->len - 4;
  return TRUE;
}

static guint8
otp_hash_step (guint8 value)
{
  for (guint bit = 0; bit < 8; bit++)
    value = (value & 0x80) != 0 ? (value << 1) ^ 0x07 : value << 1;
  return value;
}

static gboolean
verify_otp (const guint8 *otp,
            gsize         otp_length)
{
  guint8 checksum = 0;

  if (otp_length < 27)
    return FALSE;
  for (gsize i = 0; i < otp_length; i++)
    if (i != 25)
      checksum = otp_hash_step (checksum ^ otp[i]);
  return otp[25] == ((~checksum) & 0xFF);
}

static guint32
decode_chip_id (const guint8 *data)
{
  return (guint32) data[0] * 0x100 +
         (guint32) data[1] +
         (guint32) data[2] * 0x1000000 +
         (guint32) data[3] * 0x10000;
}

static gchar *
sanitize_firmware (const guint8 *data,
                   gsize         length)
{
  GString *result = g_string_sized_new (MIN (length, 80));

  for (gsize i = 0; i < length && i < 80 && data[i] != 0; i++)
    g_string_append_c (result, g_ascii_isprint (data[i]) ? data[i] : '?');
  return g_string_free (result, FALSE);
}

/* Returns the layout whose interface exposes its endpoints on @device, or
 * NULL with G_IO_ERROR_NOT_SUPPORTED when no layout matches. */
static const ProbeLayout *
find_usb_layout (GUsbDevice *device,
                 GError    **error)
{
  g_autoptr(GPtrArray) interfaces = NULL;

  interfaces = g_usb_device_get_interfaces (device, error);
  if (!interfaces)
    return NULL;

  for (gsize l = 0; l < G_N_ELEMENTS (probe_layouts); l++)
    for (guint i = 0; i < interfaces->len; i++)
      {
        const ProbeLayout *layout = &probe_layouts[l];
        GUsbInterface *iface = g_ptr_array_index (interfaces, i);
        g_autoptr(GPtrArray) endpoints = NULL;
        gboolean has_in = FALSE;
        gboolean has_out = FALSE;

        if (!layout->probes_pid (g_usb_device_get_pid (device)) ||
            g_usb_interface_get_number (iface) != layout->interface ||
            g_usb_interface_get_alternate (iface) != 0)
          continue;
        endpoints = g_usb_interface_get_endpoints (iface);
        if (!endpoints)
          continue;
        for (guint j = 0; j < endpoints->len; j++)
          {
            GUsbEndpoint *endpoint = g_ptr_array_index (endpoints, j);
            guint8 address = g_usb_endpoint_get_address (endpoint);

            has_in |= address == layout->ep_in;
            has_out |= address == layout->ep_out;
          }
        if (has_in && has_out)
          return layout;
      }

  {
    g_autoptr(GString) known = g_string_new (NULL);

    for (gsize l = 0; l < G_N_ELEMENTS (probe_layouts); l++)
      g_string_append_printf (known, "%s%s (endpoints 0x%02x/0x%02x)",
                              l ? "; " : "", probe_layouts[l].name,
                              probe_layouts[l].ep_out, probe_layouts[l].ep_in);
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                 "No known USB layout is present: %s", known->str);
  }
  return NULL;
}

/* Returns the 32-byte PSK hash in a production record of @type, or NULL
 * when the record is malformed or of another type. */
static const guint8 *
parse_production_hash (const guint8 *payload,
                       gsize         payload_length,
                       guint32       type)
{
  guint32 record_type;
  guint32 data_length;

  if (payload_length < 9 || payload[0] != 0)
    return NULL;
  record_type = payload[1] | ((guint32) payload[2] << 8) |
                ((guint32) payload[3] << 16) | ((guint32) payload[4] << 24);
  data_length = payload[5] | ((guint32) payload[6] << 8) |
                ((guint32) payload[7] << 16) | ((guint32) payload[8] << 24);
  if (record_type != type || data_length != 32 || data_length != payload_length - 9)
    return NULL;
  return payload + 9;
}

/* Checks whether @hash is the SHA-256 of @psk. */
static gboolean
is_psk_hash (const guint8 *hash,
             const Psk    *psk)
{
  g_autoptr(GChecksum) sha = g_checksum_new (G_CHECKSUM_SHA256);
  guint8 expected[32];
  gsize expected_length = sizeof (expected);

  g_checksum_update (sha, psk->bytes, sizeof (psk->bytes));
  g_checksum_get_digest (sha, expected, &expected_length);
  return memcmp (hash, expected, sizeof (expected)) == 0;
}

/* Writes the key in an imported key file's @contents into @psk. Returns FALSE
 * unless it is 64 hexadecimal digits and optional whitespace, as the driver
 * requires. */
static gboolean
parse_psk_file (const gchar *contents,
                gsize        length,
                Psk         *psk)
{
  while (length > 0 && g_ascii_isspace (contents[length - 1]))
    length--;
  if (length != GOODIX_PSK_LEN * 2)
    return FALSE;
  for (gsize i = 0; i < GOODIX_PSK_LEN; i++)
    {
      gint high = g_ascii_xdigit_value (contents[i * 2]);
      gint low = g_ascii_xdigit_value (contents[i * 2 + 1]);

      if (high < 0 || low < 0)
        return FALSE;
      psk->bytes[i] = (high << 4) | low;
    }
  return TRUE;
}

/* Reads the key the driver opens the sensor with into @psk: the imported one,
 * or else the all-zero PSK. Fails when the key file can't be read or parsed,
 * as the driver's open does. */
static gboolean
read_driver_psk (DriverPsk *psk,
                 GError   **error)
{
  g_autofree gchar *contents = NULL;
  g_autoptr(GError) read_error = NULL;
  gsize length;

  *psk = (DriverPsk) { 0 };
  if (!g_file_get_contents (GOODIX_PSK_STATE_FILE, &contents, &length, &read_error))
    {
      if (g_error_matches (read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        return TRUE;
      g_propagate_prefixed_error (error, g_steal_pointer (&read_error),
                                  "Can't read the imported key: ");
      return FALSE;
    }
  if (!parse_psk_file (contents, length, &psk->key))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "%s must contain exactly 64 hexadecimal digits; the driver "
                   "won't open the sensor with it", GOODIX_PSK_STATE_FILE);
      return FALSE;
    }
  psk->imported = TRUE;
  return TRUE;
}

/* Checks whether the sensor whose key hashes to @sensor_hash holds a key
 * other than @driver's, and if so sets @step to what fixes it. */
static gboolean
psk_needs_setup (const guint8    *sensor_hash,
                 const DriverPsk *driver,
                 SetupStep       *step)
{
  const Psk zero_psk = { 0 };

  if (is_psk_hash (sensor_hash, &driver->key))
    return FALSE;
  if (!driver->imported)
    *step = SETUP_IMPORT_KEY;
  else if (is_psk_hash (sensor_hash, &zero_psk))
    *step = SETUP_REMOVE_STALE_KEY;
  else
    *step = SETUP_REIMPORT_KEY;
  return TRUE;
}

/* Returns the instructions the report prints for @step. */
static const gchar *
setup_instructions (SetupStep step)
{
  switch (step)
    {
    case SETUP_FINISH_WINDOWS_UPDATE:
      return "The sensor is in its bootloader: Dell's Windows driver has set\n"
             "a new key and installs the firmware the next time it starts.\n"
             "Start Windows once more and sign in with your fingerprint, then\n"
             "run this check again. If Windows is gone, reset the sensor with\n"
             "Dell's Linux driver, as " GOODIX_GUIDE " describes.";
    case SETUP_IMPORT_KEY:
      return "The sensor has a key set by Dell's Windows driver, and this driver\n"
             "never changes the key on this sensor.\n"
             "- If that Windows installation still exists, import its key as\n"
             "  " GOODIX_GUIDE " describes.\n"
             "- If it is gone, the key can't be recovered: reset the sensor to the\n"
             "  all-zero key with Dell's Linux driver, as " GOODIX_GUIDE "\n"
             "  describes.";
    case SETUP_REIMPORT_KEY:
      return "The sensor's key is not the one imported at " GOODIX_PSK_STATE_FILE ":\n"
             "Windows has set a new key since. Import it again as\n"
             GOODIX_GUIDE " describes, adding --replace.";
    case SETUP_REMOVE_STALE_KEY:
      return "The sensor has the all-zero key, but another key is imported at\n"
             GOODIX_PSK_STATE_FILE ", so the driver will refuse it. Remove that\n"
             "file, or, if Windows has used the sensor since, sign in there once\n"
             "and import its new key.";
    }
  g_assert_not_reached ();
}

/* Marks @report as supported but needing @step first, with its instructions
 * in @error. */
static void
set_needs_setup (ProbeReport *report,
                 SetupStep    step,
                 GError     **error)
{
  report->result = PROBE_NEEDS_SETUP;
  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       setup_instructions (step));
}

/* Checks whether @firmware is the version @layout's sensor reports from its
 * bootloader. */
static gboolean
is_bootloader_firmware (const ProbeLayout *layout,
                        const gchar       *firmware)
{
  return layout->bootloader_mark && strstr (firmware, layout->bootloader_mark);
}

static void
finish_report (ProbeReport *report,
               const GError *error,
               const GError *cleanup_error)
{
  if (cleanup_error && report->result == PROBE_COMPATIBLE)
    report->result = PROBE_INCONCLUSIVE;
  if (error && cleanup_error)
    report->reason = g_strdup_printf ("%s; cleanup failed: %s",
                                      error->message, cleanup_error->message);
  else if (error)
    report->reason = g_strdup (error->message);
  else if (cleanup_error)
    report->reason = g_strdup_printf ("Device cleanup failed: %s",
                                      cleanup_error->message);
}

static ProbeResult
probe_device (GUsbDevice  *device,
              ProbeReport *report)
{
  const guint8 ping_payload[4] = { 0 };
  const guint8 firmware_payload[] = { 0x00, 0x00 };
  const guint8 chip_payload[] = { 0x00, 0x00, 0x00, 0x04, 0x00 };
  const guint8 otp_payload[] = { 0x00, 0x00 };
  g_autoptr(GByteArray) stream = g_byte_array_new ();
  Probe probe = { .device = device, .stream = stream };
  guint8 psk_hash_request[16];
  gsize psk_hash_request_length;
  const guint8 *psk_hash;
  const guint8 sleep_payload[] = { 0x01, 0x00 };
  g_autoptr(GByteArray) reply = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GError) cleanup_error = NULL;
  const guint8 *payload;
  gsize payload_length;
  gboolean opened = FALSE;
  gboolean claimed = FALSE;
  /* True once the sensor answers as its application, which is put to sleep
   * at the end; its bootloader is left alone. */
  gboolean sleep_at_exit = FALSE;
  DriverPsk driver_psk;
  SetupStep setup_step;

  report->result = PROBE_INCONCLUSIVE;
  probe.layout = report->layout = find_usb_layout (device, &error);
  if (!probe.layout)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        report->result = PROBE_INCOMPATIBLE;
      goto out;
    }
  if (!g_usb_device_open (device, &error))
    goto out;
  opened = TRUE;
  if (!g_usb_device_claim_interface (
        device, probe.layout->interface,
        G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, &error))
    goto out;
  claimed = TRUE;
  if (!drain_input (&probe, &error))
    goto out;

  reply = run_command (&probe, 0x00, 0x00, ping_payload,
                       probe.layout->ping_payload_length, FALSE, &error);
  if (!reply)
    goto out;
  sleep_at_exit = TRUE;

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (&probe, 0x0A, 0x04, firmware_payload,
                       sizeof (firmware_payload), TRUE, &error);
  if (!reply || !parse_reply (reply, 0x0A, 0x04, &payload,
                              &payload_length, &error))
    goto out;
  report->firmware = sanitize_firmware (payload, payload_length);
  if (is_bootloader_firmware (probe.layout, report->firmware))
    {
      /* The bootloader acknowledges the reset and chip-ID read without
       * replying, so the probe stops here. */
      sleep_at_exit = FALSE;
      set_needs_setup (report, SETUP_FINISH_WINDOWS_UPDATE, &error);
      goto out;
    }

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (&probe, 0x0A, 0x01, probe.layout->reset_payload,
                       sizeof (probe.layout->reset_payload),
                       probe.layout->reset_replies, &error);
  if (!reply || (probe.layout->reset_replies &&
                 !parse_reply (reply, 0x0A, 0x01, &payload, &payload_length, &error)))
    goto out;
  g_usleep (10 * 1000);

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (&probe, 0x08, 0x01, chip_payload,
                       sizeof (chip_payload), TRUE, &error);
  if (!reply || !parse_reply (reply, 0x08, 0x01, &payload,
                              &payload_length, &error))
    goto out;
  if (payload_length != 4)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Unexpected chip ID length: %zu", payload_length);
      goto out;
    }
  report->chip_id = decode_chip_id (payload);
  if ((report->chip_id & MILAN_CHIP_FAMILY_MASK) !=
      MILAN_CHIP_FAMILY_PREFIX)
    {
      report->result = PROBE_INCOMPATIBLE;
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "chip 0x%08x is not in the supported 0x00220cxx family",
                   report->chip_id);
      goto out;
    }

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (&probe, 0x0A, 0x03, otp_payload,
                       sizeof (otp_payload), TRUE, &error);
  if (!reply || !parse_reply (reply, 0x0A, 0x03, &payload,
                              &payload_length, &error))
    goto out;
  if (!verify_otp (payload, payload_length))
    {
      g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "OTP checksum validation failed");
      goto out;
    }

  g_clear_pointer (&reply, g_byte_array_unref);
  psk_hash_request_length = probe.layout->psk_hash_request (probe.layout->psk_hash_type,
                                                            psk_hash_request);
  reply = run_command (&probe, 0x0E, 0x02, psk_hash_request,
                       psk_hash_request_length, TRUE, &error);
  if (!reply || !parse_reply (reply, 0x0E, 0x02, &payload,
                              &payload_length, &error))
    goto out;
  psk_hash = parse_production_hash (payload, payload_length,
                                    probe.layout->psk_hash_type);
  if (!psk_hash)
    {
      g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "PSK hash production record has an unexpected format");
      goto out;
    }
  if (probe.layout->requires_driver_psk)
    {
      if (!read_driver_psk (&driver_psk, &error))
        goto out;
      if (psk_needs_setup (psk_hash, &driver_psk, &setup_step))
        {
          set_needs_setup (report, setup_step, &error);
          goto out;
        }
    }

  report->result = PROBE_COMPATIBLE;

out:
  if (sleep_at_exit)
    {
      g_autoptr(GByteArray) sleep_reply = NULL;

      sleep_reply = run_command (&probe, 0x06, 0x00, sleep_payload,
                                 sizeof (sleep_payload), FALSE, &cleanup_error);
    }
  if (claimed)
    {
      g_autoptr(GError) release_error = NULL;

      if (!g_usb_device_release_interface (
            device, probe.layout->interface,
            G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, &release_error) &&
          !cleanup_error)
        cleanup_error = g_steal_pointer (&release_error);
    }
  if (opened)
    {
      g_autoptr(GError) close_error = NULL;

      if (!g_usb_device_close (device, &close_error) && !cleanup_error)
        cleanup_error = g_steal_pointer (&close_error);
    }
  finish_report (report, error, cleanup_error);
  return report->result;
}

static gboolean
is_registered_pid (guint16 pid)
{
  return pid == 0x5335 || pid == 0x533c || pid == 0x5385 || pid == 0x5395;
}

static gchar *
read_system_name (void)
{
  g_autofree gchar *vendor = NULL;
  g_autofree gchar *product = NULL;

  if (!g_file_get_contents ("/sys/class/dmi/id/sys_vendor", &vendor, NULL,
                            NULL) ||
      !g_file_get_contents ("/sys/class/dmi/id/product_name", &product, NULL,
                            NULL))
    return NULL;
  g_strstrip (vendor);
  g_strstrip (product);
  return g_strdup_printf ("%s %s", vendor, product);
}

static void
print_report (const ProbeReport *report,
              const gchar       *system_name,
              const gchar       *kernel_release)
{
  g_print ("%sGoodix %04x:%04x%s\n", color ("\033[1;36m"), report->vid,
           report->pid, color ("\033[0m"));
  if (report->layout)
    g_print ("USB %s\n", report->layout->name);
  if (report->firmware && report->firmware[0] != '\0')
    g_print ("Firmware %s\n", report->firmware);
  if (report->chip_id != 0)
    {
      g_print ("Chip 0x%08x", report->chip_id);
      /* Reference-driver profiles and algorithm sensor types are distinct enums. */
      switch (report->chip_id & MILAN_CHIP_FAMILY_MASK)
        {
        case 0x00220200u:
          g_print (" | Milan profile 0 | sensor type 0");
          break;
        case 0x00220700u:
          g_print (" | Milan profile 1 | sensor type 6");
          break;
        case 0x00220800u:
          g_print (" | Milan profile 2 | sensor type 7");
          break;
        case MILAN_CHIP_FAMILY_PREFIX:
          g_print (" | Milan profile 9 | sensor type 12");
          break;
        default:
          break;
        }
      g_print ("\n");
    }
  if (system_name)
    g_print ("System %s | Linux %s\n", system_name, kernel_release);

  switch (report->result)
    {
    case PROBE_COMPATIBLE:
      if (is_registered_pid (report->pid))
        {
          print_result ("\033[1;32m", SYMBOL_CHECK,
                        "MATCH - USB ID already registered");
          g_print ("Hardware validation reports are welcome.\n");
        }
      else
        {
          print_result ("\033[1;32m", SYMBOL_CHECK,
                        "COMPATIBLE CANDIDATE");
          g_print ("Open a compatibility issue and paste this output.\n");
          g_print ("Developers may instead submit a PR adding USB %04x:%04x.\n",
                   report->vid, report->pid);
        }
      break;
    case PROBE_INCOMPATIBLE:
      print_result ("\033[1;31m", SYMBOL_CROSS, "NOT COMPATIBLE");
      if (report->reason)
        g_print ("%s\n", report->reason);
      break;
    case PROBE_NEEDS_SETUP:
      print_result ("\033[1;33m", SYMBOL_WARN, "SUPPORTED - SETUP NEEDED FIRST");
      g_print ("%s\n", report->reason);
      break;
    case PROBE_INCONCLUSIVE:
      print_result ("\033[1;33m", SYMBOL_WARN, "INCONCLUSIVE");
      if (report->reason)
        g_print ("%s\n", report->reason);
      if (geteuid () != 0)
        g_print ("Try again with sudo after stopping fprintd.\n");
      break;
    }
}

static gboolean
self_test (void)
{
  const guint8 payload[] = { 0x00, 0x00 };
  const guint8 chip_bytes[] = { 0x0c, 0xa1, 0x00, 0x22 };
  guint8 production_payload[41] = { 0 };
  guint8 otp[27] = { 0 };
  guint8 otp_checksum = 0;
  guint no_progress_count = 0;
  ProbeReport cleanup_report = { .result = PROBE_COMPATIBLE };
  /* SHA-256 of the all-zero PSK, as the 533c reports it. */
  const guint8 zero_psk_hash[32] = {
    0x66, 0x68, 0x7a, 0xad, 0xf8, 0x62, 0xbd, 0x77, 0x6c, 0x8f, 0xc1, 0x8b,
    0x8e, 0x9f, 0x8e, 0x20, 0x08, 0x97, 0x14, 0x85, 0x6e, 0xe2, 0x33, 0xb3,
    0x90, 0x2a, 0x59, 0x1d, 0x0d, 0x5f, 0x29, 0x25,
  };
  const gchar *psk_line = "00000000000000000000000000000000"
                          "000000000000000000000000000000ff\n";
  Psk psk;
  guint8 other_hash[32];
  DriverPsk zero = { 0 };
  DriverPsk imported = { .imported = TRUE };
  SetupStep step;
  g_autoptr(GByteArray) message = build_message (0x0A, 0x04, payload,
                                                 sizeof (payload));
  g_autoptr(GError) error = NULL;
  g_autoptr(GError) cleanup_error = NULL;

  production_payload[1] = 0x03;
  production_payload[2] = 0xB0;
  production_payload[5] = 32;
  for (gsize i = 0; i < sizeof (otp); i++)
    if (i != 25)
      otp_checksum = otp_hash_step (otp_checksum ^ otp[i]);
  otp[25] = (~otp_checksum) & 0xFF;
  if (!validate_message (message->data, message->len, &error) ||
      decode_chip_id (chip_bytes) != 0x00220ca1 ||
      otp_hash_step (0x01) != 0x07 ||
      !verify_otp (otp, sizeof (otp)) ||
      !parse_production_hash (production_payload,
                              sizeof (production_payload), GOODIX_PSK_HASH_DIRECT) ||
      parse_production_hash (production_payload,
                             sizeof (production_payload) - 1, GOODIX_PSK_HASH_DIRECT))
    return FALSE;

  if (!parse_psk_file (psk_line, strlen (psk_line), &psk) || psk.bytes[31] != 0xff ||
      parse_psk_file (psk_line, 63, &psk) ||
      parse_psk_file ("g0000000000000000000000000000000"
                      "00000000000000000000000000000000", 64, &psk))
    return FALSE;
  psk.bytes[31] = 0;
  if (!is_psk_hash (zero_psk_hash, &psk))
    return FALSE;
  psk.bytes[31] = 0xff;
  if (is_psk_hash (zero_psk_hash, &psk))
    return FALSE;

  /* A hash of no key the test uses stands in for Windows' key. */
  memcpy (other_hash, zero_psk_hash, sizeof (other_hash));
  other_hash[0] ^= 0x01;
  imported.key = psk;
  if (psk_needs_setup (zero_psk_hash, &zero, &step) ||
      !psk_needs_setup (other_hash, &zero, &step) || step != SETUP_IMPORT_KEY ||
      !psk_needs_setup (other_hash, &imported, &step) || step != SETUP_REIMPORT_KEY ||
      !psk_needs_setup (zero_psk_hash, &imported, &step) ||
      step != SETUP_REMOVE_STALE_KEY)
    return FALSE;

  for (guint i = 0; i < 8; i++)
    if (!receive_made_progress (0, &no_progress_count, &error))
      return FALSE;
  if (receive_made_progress (0, &no_progress_count, &error) ||
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))
    return FALSE;
  g_clear_error (&error);

  cleanup_error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED,
                                       "release failed");
  finish_report (&cleanup_report, NULL, cleanup_error);
  if (cleanup_report.result != PROBE_INCONCLUSIVE || !cleanup_report.reason)
    return FALSE;
  g_clear_pointer (&cleanup_report.reason, g_free);

  message->data[message->len - 1] ^= 0x01;
  g_clear_error (&error);
  return !validate_message (message->data, message->len, &error);
}

int
main (int argc, char **argv)
{
  g_autoptr(GUsbContext) context = NULL;
  g_autoptr(GPtrArray) devices = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *system_name = NULL;
  struct utsname uts = { 0 };
  gboolean found_goodix = FALSE;
  gboolean found_compatible = FALSE;
  gboolean found_definite_result = FALSE;

  if (argc == 2 && g_str_equal (argv[1], "--self-test"))
    return self_test () ? 0 : 1;
  if (argc != 1)
    {
      g_printerr ("Usage: %s\n", argv[0]);
      return 2;
    }

  use_color = isatty (STDOUT_FILENO) && g_getenv ("NO_COLOR") == NULL &&
              !g_str_equal (g_getenv ("TERM") ? g_getenv ("TERM") : "",
                            "dumb");
  system_name = read_system_name ();
  uname (&uts);
  context = g_usb_context_new (&error);
  if (!context)
    goto fail;
  g_usb_context_enumerate (context);
  devices = g_usb_context_get_devices (context);
  for (guint i = 0; i < devices->len; i++)
    {
      GUsbDevice *candidate = g_ptr_array_index (devices, i);
      ProbeReport report = { 0 };

      if (g_usb_device_get_vid (candidate) != GOODIX_VID)
        continue;
      found_goodix = TRUE;
      report.vid = g_usb_device_get_vid (candidate);
      report.pid = g_usb_device_get_pid (candidate);
      probe_device (candidate, &report);
      print_report (&report, system_name, uts.release);
      found_compatible |= report.result == PROBE_COMPATIBLE;
      found_definite_result |= report.result != PROBE_INCONCLUSIVE;
      g_clear_pointer (&report.firmware, g_free);
      g_clear_pointer (&report.reason, g_free);
      g_print ("\n");
    }

  if (!found_goodix)
    {
      fputs (SYMBOL_WARN " ", stderr);
      g_printerr ("INCONCLUSIVE: no Goodix USB device found\n");
      return 2;
    }
  if (found_compatible)
    return 0;
  return found_definite_result ? 1 : 2;

fail:
  fputs (SYMBOL_WARN " ", stderr);
  g_printerr ("INCONCLUSIVE: %s\n", error ? error->message : "USB error");
  return 2;
}
