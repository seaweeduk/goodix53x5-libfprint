/* Read the Windows PSK from a Goodix 53x5 sensor, or the DPAPI-sealed PSK
 * from a 533c, without making persistent writes. This is a migration
 * utility, not driver code. */

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gusb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define GOODIX_VID 0x27c6
#define GOODIX_INTERFACE 1
#define GOODIX_EP_OUT 0x03
#define GOODIX_EP_IN 0x81
#define GOODIX_CHUNK_SIZE 64
#define GOODIX_TIMEOUT_MS 2000
#define GOODIX_MAX_MESSAGE (64 * 1024)
#define GOODIX_READ_SEALED_PSK 0xB001
/* The 533c's GM168SEC wraps each message in a pack, [0xa0][length LE16]
 * [sum of those three bytes], zero-padded to whole USB chunks. */
#define GM168SEC_PACK_MESSAGE 0xa0
#define GM168SEC_INTERFACE 0
#define GM168SEC_EP_OUT 0x01
#define GM168SEC_EP_IN 0x83
#define GM168SEC_PACK_HEADER 4
#define GM168SEC_SEALED_PSK 0xbb010002
#define GM168SEC_PSK_HASH 0xbb020001
/* The vendor's buffer size for the sealed PSK. */
#define DPAPI_MAX_SIZE 0x7f8

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
bulk_read (GUsbDevice *device,
           guint8      endpoint,
           guint8     *chunk,
           gsize      *actual_length,
           guint       timeout,
           GError    **error)
{
  return g_usb_device_bulk_transfer (device, endpoint, chunk,
                                     GOODIX_CHUNK_SIZE, actual_length,
                                     timeout, NULL, error);
}

static gboolean
drain_input (GUsbDevice *device,
             guint8      endpoint,
             GError    **error)
{
  guint8 chunk[GOODIX_CHUNK_SIZE];

  for (guint i = 0; i < 64; i++)
    {
      gsize actual_length = 0;
      g_autoptr(GError) read_error = NULL;

      if (bulk_read (device, endpoint, chunk, &actual_length, 50, &read_error))
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

/* Takes the next pack's message off @stream, reading more as needed. Bytes
 * past it, such as a reply sent with its ACK, stay on @stream. */
static GByteArray *
receive_wrapped_message (GUsbDevice *device,
                         GByteArray *stream,
                         GError    **error)
{
  g_autoptr(GByteArray) message = NULL;
  gsize length = 0;

  for (;;)
    {
      guint8 chunk[GOODIX_CHUNK_SIZE];
      gsize actual_length = 0;
      gsize padding = 0;

      while (padding < stream->len && stream->data[padding] == 0)
        padding++;
      g_byte_array_remove_range (stream, 0, padding);
      if (stream->len >= GM168SEC_PACK_HEADER)
        {
          if (stream->data[0] != GM168SEC_PACK_MESSAGE ||
              stream->data[3] != (guint8) (stream->data[0] + stream->data[1] +
                                           stream->data[2]))
            {
              g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                   "Invalid Goodix pack header");
              return NULL;
            }
          length = stream->data[1] | ((gsize) stream->data[2] << 8);
          if (stream->len >= GM168SEC_PACK_HEADER + length)
            break;
        }
      if (!bulk_read (device, GM168SEC_EP_IN, chunk, &actual_length,
                      GOODIX_TIMEOUT_MS, error))
        return NULL;
      g_byte_array_append (stream, chunk, actual_length);
    }

  message = g_byte_array_sized_new (length);
  g_byte_array_append (message, stream->data + GM168SEC_PACK_HEADER, length);
  g_byte_array_remove_range (stream, 0, GM168SEC_PACK_HEADER + length);
  if (!validate_message (message->data, message->len, error))
    return NULL;
  return g_steal_pointer (&message);
}

/* Returns the next message, from @stream when the device uses packs. */
static GByteArray *
receive_message (GUsbDevice *device,
                 GByteArray *stream,
                 GError    **error)
{
  g_autoptr(GByteArray) message = g_byte_array_new ();
  guint8 chunk[GOODIX_CHUNK_SIZE];
  guint8 command_byte = 0;
  gsize expected = 0;

  if (stream)
    return receive_wrapped_message (device, stream, error);

  while (message->len < expected || expected == 0)
    {
      gsize actual_length = 0;
      gsize data_offset = 0;
      gsize append_length;

      if (!bulk_read (device, GOODIX_EP_IN, chunk, &actual_length,
                      GOODIX_TIMEOUT_MS, error))
        return NULL;
      if (actual_length == 0)
        continue;

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
      g_byte_array_append (message, chunk + data_offset, append_length);
    }

  if (!validate_message (message->data, message->len, error))
    return NULL;
  return g_steal_pointer (&message);
}

/* Sends a command and takes its ACK and, when @expect_reply, its reply.
 * A non-NULL @stream selects the 533c's packs and endpoints. */
static GByteArray *
run_command (GUsbDevice   *device,
             GByteArray   *stream,
             guint8        category,
             guint8        command,
             const guint8 *payload,
             gsize         payload_length,
             gboolean      expect_reply,
             GError      **error)
{
  g_autoptr(GByteArray) request = NULL;
  g_autoptr(GByteArray) ack = NULL;
  guint8 transfer[GOODIX_CHUNK_SIZE] = { 0 };
  guint8 command_byte;
  gsize actual_length = 0;
  gsize header = stream ? GM168SEC_PACK_HEADER : 0;

  request = build_message (category, command, payload, payload_length);
  if (header + request->len > sizeof (transfer))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "Migration utility command exceeds one USB chunk");
      return NULL;
    }

  if (stream)
    {
      transfer[0] = GM168SEC_PACK_MESSAGE;
      transfer[1] = request->len & 0xFF;
      transfer[2] = request->len >> 8;
      transfer[3] = transfer[0] + transfer[1] + transfer[2];
    }
  memcpy (transfer + header, request->data, request->len);
  if (!g_usb_device_bulk_transfer (device,
                                   stream ? GM168SEC_EP_OUT : GOODIX_EP_OUT,
                                   transfer,
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
  ack = receive_message (device, stream, error);
  if (!ack)
    return NULL;
  if ((ack->data[0] >> 4) != 0x0B ||
      ((ack->data[0] & 0x0F) >> 1) != 0 ||
      ack->len < 6 || ack->data[3] != command_byte ||
      (ack->data[4] & 1) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Invalid ACK for Goodix command 0x%02x", command_byte);
      return NULL;
    }

  if (!expect_reply)
    return g_byte_array_new ();
  return receive_message (device, stream, error);
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

static gboolean
read_sealed_psk (GUsbDevice  *device,
                 guint8     **sealed_psk,
                 gsize       *sealed_psk_length,
                 gboolean    *matches_psk_hash,
                 GError     **error)
{
  const guint8 ping_payload[] = { 0x00, 0x00 };
  const guint8 firmware_payload[] = { 0x00, 0x00 };
  const guint8 reset_payload[] = { 0x01, 0x14 };
  guint8 read_payload[] = { 0x01, 0xB0, 0x00, 0x00 };
  g_autoptr(GByteArray) reply = NULL;
  const guint8 *payload;
  gsize payload_length;
  guint32 type;
  guint32 data_length;

  reply = run_command (device, NULL, 0x00, 0x00, ping_payload,
                       sizeof (ping_payload), FALSE, error);
  if (!reply)
    return FALSE;

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (device, NULL, 0x0A, 0x04, firmware_payload,
                       sizeof (firmware_payload), TRUE, error);
  if (!reply || !parse_reply (reply, 0x0A, 0x04, &payload,
                              &payload_length, error))
    return FALSE;

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (device, NULL, 0x0A, 0x01, reset_payload,
                       sizeof (reset_payload), FALSE, error);
  if (!reply)
    return FALSE;
  g_usleep (10 * 1000);

  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (device, NULL, 0x0E, 0x02, read_payload,
                       sizeof (read_payload), TRUE, error);
  if (!reply || !parse_reply (reply, 0x0E, 0x02, &payload,
                              &payload_length, error))
    return FALSE;
  if (payload_length < 9 || payload[0] != 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Goodix sealed PSK read failed");
      return FALSE;
    }

  type = payload[1] | ((guint32) payload[2] << 8) |
         ((guint32) payload[3] << 16) | ((guint32) payload[4] << 24);
  data_length = payload[5] | ((guint32) payload[6] << 8) |
                ((guint32) payload[7] << 16) | ((guint32) payload[8] << 24);
  if (type != GOODIX_READ_SEALED_PSK || data_length != payload_length - 9 ||
      data_length == 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Malformed Goodix sealed PSK reply");
      return FALSE;
    }

  *sealed_psk = g_memdup2 (payload + 9, data_length);
  *sealed_psk_length = data_length;

  read_payload[0] = 0x03;
  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (device, NULL, 0x0E, 0x02, read_payload,
                       sizeof (read_payload), TRUE, error);
  if (!reply || !parse_reply (reply, 0x0E, 0x02, &payload,
                              &payload_length, error))
    return FALSE;
  if (payload_length < 9 || payload[0] != 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Goodix PSK hash read failed");
      return FALSE;
    }

  type = payload[1] | ((guint32) payload[2] << 8) |
         ((guint32) payload[3] << 16) | ((guint32) payload[4] << 24);
  data_length = payload[5] | ((guint32) payload[6] << 8) |
                ((guint32) payload[7] << 16) | ((guint32) payload[8] << 24);
  if (type != 0xB003 || data_length != payload_length - 9 || data_length != 32)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Malformed Goodix PSK hash reply");
      return FALSE;
    }

  if (*sealed_psk_length == 32)
    {
      g_autoptr(GChecksum) checksum = g_checksum_new (G_CHECKSUM_SHA256);
      guint8 digest[32];
      gsize digest_length = sizeof (digest);

      g_checksum_update (checksum, *sealed_psk, *sealed_psk_length);
      g_checksum_get_digest (checksum, digest, &digest_length);
      *matches_psk_hash = memcmp (digest, payload + 9, sizeof (digest)) == 0;
    }
  return TRUE;
}

static guint32
get_u32_le (const guint8 *data)
{
  return data[0] | ((guint32) data[1] << 8) |
         ((guint32) data[2] << 16) | ((guint32) data[3] << 24);
}

/* Reads @length bytes at @offset of 533c preset @slot:
 * E4 requests [count, offset, slot, 0] of at most 256 bytes, each answered
 * by [status][slot LE32][count LE32] and the bytes. */
static gboolean
read_preset (GUsbDevice *device,
             GByteArray *stream,
             guint32     slot,
             guint32     offset,
             guint8     *data,
             guint32     length,
             GError    **error)
{
  while (length > 0)
    {
      guint32 count = MIN (length, 256);
      guint32 request[] = { GUINT32_TO_LE (count), GUINT32_TO_LE (offset),
                            GUINT32_TO_LE (slot), 0 };
      g_autoptr(GByteArray) reply = NULL;
      const guint8 *payload;
      gsize payload_length;

      reply = run_command (device, stream, 0x0E, 0x02, (const guint8 *) request,
                           sizeof (request), TRUE, error);
      if (!reply || !parse_reply (reply, 0x0E, 0x02, &payload,
                                  &payload_length, error))
        return FALSE;
      if (payload_length != 9 + count || payload[0] != 0 ||
          get_u32_le (payload + 1) != slot || get_u32_le (payload + 5) != count)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Malformed Goodix preset 0x%08x reply", slot);
          return FALSE;
        }
      memcpy (data, payload + 9, count);
      data += count;
      offset += count;
      length -= count;
    }
  return TRUE;
}

/* The read reply carries only a fragment length, and Windows sizes the read
 * by sealing a dummy key. Dell's driver seals 32 bytes with a fixed
 * description, so every DPAPI length field up to the encrypted data is in the
 * first 256 bytes; the HMAC that follows is as long as the blob's hash. That
 * gives the size for Windows' own reads: 256 bytes, then the remainder. */
static gboolean
read_dpapi_blob (GUsbDevice  *device,
                 GByteArray  *stream,
                 guint8     **blob,
                 gsize       *blob_length,
                 GError     **error)
{
  /* Version 1 and the only DPAPI provider. */
  static const guint8 prefix[] = { 1, 0, 0, 0, 0xd0, 0x8c, 0x9d, 0xdf, 1, 0x15,
                                   0xd1, 0x11, 0x8c, 0x7a, 0, 0xc0, 0x4f, 0xc2,
                                   0x97, 0xeb };
  /* Fixed fields ending in the lengths of the description, salt, HMAC key,
   * second HMAC key and data. */
  static const guint8 fixed[] = { 48, 12, 4, 12, 4 };
  const guint32 first = 256;
  g_autofree guint8 *data = g_malloc (DPAPI_MAX_SIZE);
  guint32 length = 0;
  guint32 sign_length = 0;
  guint32 total;

  if (!read_preset (device, stream, GM168SEC_SEALED_PSK, 0, data, first, error))
    return FALSE;
  if (memcmp (data, prefix, sizeof (prefix)) != 0)
    goto malformed;
  for (guint i = 0; i < G_N_ELEMENTS (fixed); i++)
    {
      guint32 field;

      if (fixed[i] > first - length)
        goto malformed;
      length += fixed[i];
      field = get_u32_le (data + length - 4);
      if (i == 3)
        sign_length = get_u32_le (data + length - 8) / 8;
      if (field > first - length)
        goto malformed;
      length += field;
    }

  total = length + 4 + sign_length;
  if (total > DPAPI_MAX_SIZE)
    goto malformed;
  if (total > first &&
      !read_preset (device, stream, GM168SEC_SEALED_PSK, first, data + first,
                    total - first, error))
    return FALSE;
  if (get_u32_le (data + length) != sign_length)
    goto malformed;

  *blob = g_steal_pointer (&data);
  *blob_length = total;
  return TRUE;

malformed:
  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "The sealed PSK is not a DPAPI blob");
  return FALSE;
}

/* After the driver's opening ping and firmware query, reads the 533c's
 * sealed PSK into @blob or, given @key, checks that the sensor's PSK hash
 * is its SHA-256. */
static gboolean
read_533c (GUsbDevice   *device,
           GByteArray   *stream,
           const guint8 *key,
           guint8      **blob,
           gsize        *blob_length,
           GError      **error)
{
  static const guint8 zero_key[32] = { 0 };
  const guint8 ping_payload[4] = { 0 };
  const guint8 firmware_payload[] = { 0x00, 0x00 };
  g_autoptr(GByteArray) reply = NULL;
  g_autoptr(GChecksum) checksum = NULL;
  const guint8 *payload;
  gsize payload_length;
  guint8 hash[32];
  guint8 digest[32];
  gsize digest_length = sizeof (digest);

  reply = run_command (device, stream, 0x00, 0x00, ping_payload,
                       sizeof (ping_payload), FALSE, error);
  if (!reply)
    return FALSE;
  g_clear_pointer (&reply, g_byte_array_unref);
  reply = run_command (device, stream, 0x0A, 0x04, firmware_payload,
                       sizeof (firmware_payload), TRUE, error);
  if (!reply || !parse_reply (reply, 0x0A, 0x04, &payload,
                              &payload_length, error))
    return FALSE;

  if (!read_preset (device, stream, GM168SEC_PSK_HASH, 0, hash, sizeof (hash),
                    error))
    return FALSE;
  checksum = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (checksum, key ? key : zero_key, 32);
  g_checksum_get_digest (checksum, digest, &digest_length);

  if (!key)
    {
      if (memcmp (digest, hash, sizeof (hash)) == 0)
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                               "The sensor still holds the default Linux key, "
                               "so there is no Windows key to import yet. "
                               "Sign in with your fingerprint in Windows first");
          return FALSE;
        }
      return read_dpapi_blob (device, stream, blob, blob_length, error);
    }
  if (memcmp (digest, hash, sizeof (hash)) != 0)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "The key's SHA-256 is not the sensor's PSK hash");
      return FALSE;
    }
  return TRUE;
}

static gboolean
write_private_file (const gchar  *path,
                    const guint8 *data,
                    gsize         length,
                    GError      **error)
{
  gint fd = g_open (path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    0600);
  gsize offset = 0;

  if (fd < 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "Cannot create %s: %s", path, g_strerror (errno));
      return FALSE;
    }

  while (offset < length)
    {
      ssize_t written = write (fd, data + offset, length - offset);

      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        {
          gint saved_errno = written < 0 ? errno : EIO;
          close (fd);
          g_unlink (path);
          g_set_error (error, G_FILE_ERROR,
                       g_file_error_from_errno (saved_errno),
                       "Cannot write %s: %s", path,
                       g_strerror (saved_errno));
          return FALSE;
        }
      offset += written;
    }

  if (fsync (fd) != 0)
    {
      gint saved_errno = errno;
      close (fd);
      g_unlink (path);
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Cannot finish %s: %s", path, g_strerror (saved_errno));
      return FALSE;
    }
  if (close (fd) != 0)
    {
      gint saved_errno = errno;
      g_unlink (path);
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno),
                   "Cannot close %s: %s", path, g_strerror (saved_errno));
      return FALSE;
    }
  return TRUE;
}

static gboolean
self_test (void)
{
  guint8 data[80];
  guint8 payload[89] = { 0 };
  g_autoptr(GByteArray) message = NULL;
  const guint8 *parsed;
  gsize parsed_length;
  g_autoptr(GError) error = NULL;

  for (guint i = 0; i < sizeof (data); i++)
    data[i] = i;
  payload[1] = 0x01;
  payload[2] = 0xB0;
  payload[5] = sizeof (data);
  memcpy (payload + 9, data, sizeof (data));
  message = build_message (0x0E, 0x02, payload, sizeof (payload));

  return validate_message (message->data, message->len, &error) &&
         parse_reply (message, 0x0E, 0x02, &parsed, &parsed_length, &error) &&
         parsed_length == sizeof (payload) &&
         memcmp (parsed + 9, data, sizeof (data)) == 0;
}

/* Reads the PSK or, on a 533c (@wrapped), the sealed PSK; given @key, only
 * checks it against the 533c's PSK hash. */
static gboolean
read_device_psk (GUsbDevice   *device,
                 gboolean      wrapped,
                 const guint8 *key,
                 guint8      **psk,
                 gsize        *psk_length,
                 GError      **error)
{
  guint8 interface = wrapped ? GM168SEC_INTERFACE : GOODIX_INTERFACE;
  g_autoptr(GByteArray) stream = wrapped ? g_byte_array_new () : NULL;
  gboolean opened = FALSE;
  gboolean claimed = FALSE;
  gboolean matches_psk_hash = FALSE;
  gboolean success = FALSE;

  if (!g_usb_device_open (device, error))
    goto out;
  opened = TRUE;
  if (!g_usb_device_claim_interface (
        device, interface,
        G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, error))
    goto out;
  claimed = TRUE;

  if (wrapped)
    {
      success = drain_input (device, GM168SEC_EP_IN, error) &&
                read_533c (device, stream, key, psk, psk_length, error);
      goto out;
    }
  if (!drain_input (device, GOODIX_EP_IN, error) ||
      !read_sealed_psk (device, psk, psk_length, &matches_psk_hash, error))
    goto out;
  if (!matches_psk_hash)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "B001 is not the active plaintext PSK");
      goto out;
    }

  success = TRUE;

out:
  if (claimed)
    g_usb_device_release_interface (
      device, interface,
      G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, NULL);
  if (opened)
    g_usb_device_close (device, NULL);
  return success;
}

/* Reads a key as 64 hexadecimal digits on standard input. */
static gboolean
read_hex_key (guint8 key[32])
{
  gchar line[66];

  if (!fgets (line, sizeof (line), stdin) || strlen (g_strstrip (line)) != 64)
    return FALSE;
  for (guint i = 0; i < 32; i++)
    {
      gint high = g_ascii_xdigit_value (line[2 * i]);
      gint low = g_ascii_xdigit_value (line[2 * i + 1]);

      if (high < 0 || low < 0)
        return FALSE;
      key[i] = (high << 4) | low;
    }
  return TRUE;
}

int
main (int argc, char **argv)
{
  g_autoptr(GUsbContext) context = NULL;
  g_autoptr(GPtrArray) devices = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree guint8 *psk = NULL;
  gsize psk_length = 0;
  gboolean found_goodix = FALSE;
  gboolean found_compatible = FALSE;
  gint status = 1;
  gboolean verify_533c = argc == 2 && g_str_equal (argv[1], "--verify-533c");
  gboolean wrapped = verify_533c ||
                     (argc == 3 && g_str_equal (argv[1], "--533c"));
  const gchar *output = argv[argc - 1];
  guint8 key[32];

  if (argc == 2 && g_str_equal (argv[1], "--self-test"))
    return self_test () ? 0 : 1;
  if (!wrapped && argc != 2)
    {
      g_printerr ("Usage: %s [--533c] OUTPUT_FILE | --verify-533c < HEX_KEY\n",
                  argv[0]);
      return 2;
    }
  if (verify_533c && !read_hex_key (key))
    {
      g_printerr ("error: expected 64 hexadecimal digits on standard input\n");
      return 2;
    }

  context = g_usb_context_new (&error);
  if (!context)
    goto out;
  g_usb_context_enumerate (context);
  devices = g_usb_context_get_devices (context);
  for (guint i = 0; i < devices->len; i++)
    {
      GUsbDevice *candidate = g_ptr_array_index (devices, i);
      g_autoptr(GError) candidate_error = NULL;

      if (g_usb_device_get_vid (candidate) != GOODIX_VID ||
          (wrapped && g_usb_device_get_pid (candidate) != 0x533c))
        continue;
      found_goodix = TRUE;
      if (read_device_psk (candidate, wrapped, verify_533c ? key : NULL,
                           &psk, &psk_length, &candidate_error))
        {
          found_compatible = TRUE;
          break;
        }

      g_printerr ("Skipping Goodix %04x:%04x: %s\n",
                  g_usb_device_get_vid (candidate),
                  g_usb_device_get_pid (candidate),
                  candidate_error ? candidate_error->message :
                  "unknown protocol error");
      g_clear_pointer (&psk, g_free);
      psk_length = 0;
    }

  if (!found_compatible)
    {
      g_set_error_literal (&error, G_USB_DEVICE_ERROR,
                           found_goodix ? G_USB_DEVICE_ERROR_NOT_SUPPORTED :
                           G_USB_DEVICE_ERROR_NO_DEVICE,
                           found_goodix ?
                           "No compatible Goodix fingerprint sensor found" :
                           "No Goodix USB device found");
      goto out;
    }
  if (verify_533c)
    {
      g_print ("The key matches the sensor\n");
      status = 0;
      goto out;
    }
  if (!write_private_file (output, psk, psk_length, &error))
    goto out;

  if (wrapped)
    g_print ("Saved the sealed Windows key to %s\n", output);
  else
    {
      g_print ("Saved verified %zu-byte Windows PSK to %s\n",
               psk_length, output);
      g_print ("B001 SHA-256 matches the active B003 PSK hash\n");
    }
  status = 0;

out:
  if (error)
    g_printerr ("error: %s\n", error->message);
  return status;
}
