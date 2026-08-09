/*
 * Reduced Goodix Milan profile-9 Windows oracle.
 *
 * This keeps the validated normal identifyImage extraction/call order and the
 * one documented deterministic replacement: the DLL-owned 2,288-byte feature
 * matrix is replaced by a DLL-owned 2,289-byte shadow whose final byte is zero.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define SENSOR_ROWS 88
#define SENSOR_COLUMNS 108
#define PIXEL_COUNT (SENSOR_ROWS * SENSOR_COLUMNS)
#define RAW_FRAME_SIZE (PIXEL_COUNT * sizeof (uint16_t))
#define PROCESSED_FRAME_SIZE PIXEL_COUNT
#define PREPROCESS_AUX_SIZE 0x4c98
#define MAX_TEMPLATE_SIZE (16 * 1024 * 1024)

#define PROFILE 9
#define SUBTYPE 12
#define T_CODE 0x79
#define DAC_HIGH 0x7d
#define DAC_LOW 0xc6
#define ANTIFAKE_MODE 1
#define PURPOSE_IDENTIFY 0
#define PURPOSE_ENROLL 1

/* These RVAs are valid only for the case-pinned 2.0.310.900 DLL identity. */
#define IDENTIFY_PRE_ANTIFAKE_RVA 0x1dbd
#define PROBE_GLOBAL_RVA 0x24ebe8
#define MALLOC_IAT_RVA 0x9a328
#define FREE_IAT_RVA 0x9a330
#define FEATURE_DELETE_RVA 0x37b10
#define QUEUE_RANKS_OFFSET 0x8db0
#define QUEUE_CAPACITY 20
#define FEATURE_MATRIX_OFFSET 0x20
#define FEATURE_MATRIX_BYTES 2288
#define PREPROCESS_RETRY_RAW_ADMISSION 0x29aa
#define PREPROCESS_RETRY_VALIDATION_B 0x29bb
#define PREPROCESS_RETRY_STATEFUL     0x7531
#define PREPROCESS_RETRY_CLASSIFICATION 0xc351
#define FEATURE_MATRIX_ALLOCATION (0x20 + FEATURE_MATRIX_BYTES)
#define FEATURE_MATRIX_SHADOW_ALLOCATION (FEATURE_MATRIX_ALLOCATION + 1)
#define FEATURE_RECORD_LIMIT 150
#define FEATURE_RECORD_SIZE 0x38
#define TEMPLATE_TAIL_SIZE 0x520

typedef struct
{
  uint8_t reserved[0x18];
  const uint16_t *frame;
  uint32_t unused;
  uint32_t rows;
  uint32_t columns;
} preprocessor_setup;

typedef struct
{
  void *data;
  uint8_t format[12];
  uint32_t capacity;
  uint16_t valid;
  uint16_t reserved_1a;
  int32_t purpose;
  uint8_t reserved_20[8];
  uint8_t quality;
  uint8_t coverage;
  uint8_t reserved_2a[6];
} image_descriptor;

typedef struct
{
  int32_t width;
  int32_t height;
  int32_t stride;
  int32_t size;
  int32_t element_size;
  int32_t reserved;
  uint8_t *data;
} matrix_header;

typedef int32_t (*get_algorithm_version_fn) (char *version);
typedef int32_t (*ppp_param_init_fn) (int32_t sensor_type);
typedef int32_t (*preprocess_init_calidata_fn) (void);
typedef int32_t (*preprocessor_get_calib_param_fn) (void **data,
                                                     uint32_t *size);
typedef int32_t (*preprocessor_init_fn) (const preprocessor_setup *setup);
typedef int32_t (*preprocessor_fn) (const image_descriptor *source,
                                    const int32_t *purpose,
                                    void *auxiliary,
                                    image_descriptor *result,
                                    int32_t quality_coverage[2],
                                    uint8_t liveness_switch,
                                    uint8_t mode_switch);
typedef int32_t (*preprocessor_exit_fn) (void);
typedef int32_t (*template_get_packed_size_fn) (void *template_object);
typedef int32_t (*template_pack_fn) (void *template_object, void *output);
typedef int32_t (*template_unpack_fn) (const void *packed,
                                       int32_t length,
                                       void *context,
                                       void **template_out);
typedef void (*template_delete_fn) (void *template_object);
typedef int32_t (*identify_image_fn) (image_descriptor *image,
                                      void *auxiliary,
                                      void **candidates,
                                      uint32_t candidate_count,
                                      uint32_t *matched_index,
                                      int32_t *score,
                                      uint32_t quality_coverage[2],
                                      uint32_t flags,
                                      uint8_t mode,
                                      void *calibration,
                                      const uint16_t *raw_frame,
                                      uint16_t t_code,
                                      uint16_t dac_high,
                                      uint16_t dac_low,
                                      uint32_t anti_fake_mode);
typedef int32_t (*template_study_fn) (int32_t *updated);
typedef void *(*dll_malloc_fn) (size_t size);
typedef void (*dll_free_fn) (void *memory);
typedef int32_t (*feature_delete_fn) (void **feature);
typedef struct
{
  HMODULE module;
  get_algorithm_version_fn get_algorithm_version;
  ppp_param_init_fn ppp_param_init;
  preprocess_init_calidata_fn preprocess_init_calidata;
  preprocessor_get_calib_param_fn preprocessor_get_calib_param;
  preprocessor_init_fn preprocessor_init;
  preprocessor_fn preprocessor;
  preprocessor_exit_fn preprocessor_exit;
  template_get_packed_size_fn template_get_packed_size;
  template_pack_fn template_pack;
  template_unpack_fn template_unpack;
  template_delete_fn template_delete;
  identify_image_fn identify_image;
  template_study_fn template_study;
  void *calibration;
  uint32_t calibration_size;
  uint8_t *auxiliary;
  int preprocessor_started;
} oracle_api;

typedef struct
{
  uint8_t *data;
  int32_t size;
} packed_template;

_Static_assert (offsetof (preprocessor_setup, frame) == 0x18,
                "setup frame offset mismatch");
_Static_assert (offsetof (preprocessor_setup, rows) == 0x24,
                "setup row offset mismatch");
_Static_assert (offsetof (image_descriptor, purpose) == 0x1c,
                "descriptor purpose offset mismatch");

static const uint8_t source_format[12] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t result_format[12] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
};

static HMODULE boundary_module;
static uint8_t *boundary_site;
static uint8_t boundary_saved_byte;
static uint8_t *boundary_rearm_site;
static dll_malloc_fn boundary_malloc;
static dll_free_fn boundary_free;
static int boundary_hook_hits;
static int boundary_error;
static int boundary_record_count;

static int
resolve (HMODULE module, const char *name, void *target, size_t target_size)
{
  FARPROC address = GetProcAddress (module, name);

  if (!address || target_size != sizeof (address))
    return 0;
  memcpy (target, &address, sizeof (address));
  return 1;
}

static uint8_t *
read_file (const wchar_t *path, size_t expected_size, size_t *size_out)
{
  FILE *input = _wfopen (path, L"rb");
  uint8_t *data = NULL;
  long length;

  if (!input || fseek (input, 0, SEEK_END) != 0 ||
      (length = ftell (input)) < 0 || fseek (input, 0, SEEK_SET) != 0 ||
      (expected_size && (size_t) length != expected_size))
    goto out;
  data = malloc (length ? (size_t) length : 1);
  if (!data || (length && fread (data, 1, (size_t) length, input) !=
                         (size_t) length))
    {
      free (data);
      data = NULL;
    }
  else if (size_out)
    *size_out = (size_t) length;
out:
  if (input)
    fclose (input);
  return data;
}

static uint16_t *
read_frame (const wchar_t *path)
{
  uint16_t *frame = (uint16_t *) read_file (path, RAW_FRAME_SIZE, NULL);

  if (!frame)
    return NULL;
  for (size_t i = 0; i < PIXEL_COUNT; i++)
    if (frame[i] > 0x0fff)
      {
        free (frame);
        return NULL;
      }
  return frame;
}

static int
join_path (wchar_t path[32768], const wchar_t *directory, const wchar_t *name)
{
  int length = swprintf (path, 32768, L"%ls\\%ls", directory, name);

  return length >= 0 && length < 32768;
}

static int
write_file (const wchar_t *directory, const wchar_t *name,
            const void *data, size_t size)
{
  wchar_t path[32768];
  wchar_t temporary[32768];
  FILE *output;

  if (!join_path (path, directory, name) ||
      swprintf (temporary, 32768, L"%ls.tmp", path) < 0)
    return 0;
  output = _wfopen (temporary, L"wb");
  if (!output)
    return 0;
  int write_ok = !size || fwrite (data, 1, size, output) == size;
  int flush_ok = write_ok && fflush (output) == 0;
  int close_ok = fclose (output) == 0;
  if (!write_ok || !flush_ok || !close_ok ||
      !MoveFileExW (temporary, path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
      _wremove (temporary);
      return 0;
    }
  return 1;
}

static int
open_api (oracle_api *api, const wchar_t *dll_path)
{
  char version[128] = { 0 };

  memset (api, 0, sizeof (*api));
  api->module = LoadLibraryW (dll_path);
  if (!api->module ||
      !resolve (api->module, "getAlgorithmVersion", &api->get_algorithm_version,
                sizeof (api->get_algorithm_version)) ||
      !resolve (api->module, "ppp_param_init", &api->ppp_param_init,
                sizeof (api->ppp_param_init)) ||
      !resolve (api->module, "preprocess_init_calidata",
                &api->preprocess_init_calidata,
                sizeof (api->preprocess_init_calidata)) ||
      !resolve (api->module, "preprocessor_get_CalibParam",
                &api->preprocessor_get_calib_param,
                sizeof (api->preprocessor_get_calib_param)) ||
      !resolve (api->module, "preprocessor_init", &api->preprocessor_init,
                sizeof (api->preprocessor_init)) ||
      !resolve (api->module, "preprocessor", &api->preprocessor,
                sizeof (api->preprocessor)) ||
      !resolve (api->module, "preprocessor_exit", &api->preprocessor_exit,
                sizeof (api->preprocessor_exit)) ||
      !resolve (api->module, "templateGetPackedSize",
                &api->template_get_packed_size,
                sizeof (api->template_get_packed_size)) ||
      !resolve (api->module, "templatePack", &api->template_pack,
                sizeof (api->template_pack)) ||
      !resolve (api->module, "templateUnPack", &api->template_unpack,
                sizeof (api->template_unpack)) ||
      !resolve (api->module, "templateDelete", &api->template_delete,
                sizeof (api->template_delete)) ||
      !resolve (api->module, "identifyImage", &api->identify_image,
                sizeof (api->identify_image)) ||
      !resolve (api->module, "templateStudy", &api->template_study,
                sizeof (api->template_study)) ||
      api->get_algorithm_version (version) != 0 ||
      api->ppp_param_init (PROFILE) != 0 ||
      api->preprocess_init_calidata () != 0 ||
      api->preprocessor_get_calib_param (&api->calibration,
                                         &api->calibration_size) != 0 ||
      !api->calibration || !api->calibration_size)
    return 0;
  api->auxiliary = calloc (PREPROCESS_AUX_SIZE, 1);
  return api->auxiliary != NULL;
}

static void
close_api (oracle_api *api)
{
  if (api->preprocessor_started)
    api->preprocessor_exit ();
  free (api->auxiliary);
  if (api->module)
    FreeLibrary (api->module);
  memset (api, 0, sizeof (*api));
}

static int
start_generation (oracle_api *api, const uint16_t *base)
{
  preprocessor_setup setup = { 0 };

  setup.frame = base;
  setup.rows = SENSOR_ROWS;
  setup.columns = SENSOR_COLUMNS;
  if (api->preprocessor_init (&setup) != 0)
    return 0;
  api->preprocessor_started = 1;
  return 1;
}

static int32_t
preprocess_frame (oracle_api *api, uint16_t *raw,
                  uint8_t processed_data[PROCESSED_FRAME_SIZE],
                  image_descriptor *processed,
                  int32_t quality_coverage[2], int32_t purpose)
{
  image_descriptor source = { 0 };

  if (purpose != PURPOSE_IDENTIFY && purpose != PURPOSE_ENROLL)
    return -1;

  source.data = raw;
  memcpy (source.format, source_format, sizeof (source.format));
  source.capacity = RAW_FRAME_SIZE;
  source.valid = 1;
  source.purpose = purpose;
  memset (processed, 0, sizeof (*processed));
  processed->data = processed_data;
  memcpy (processed->format, result_format, sizeof (processed->format));
  memcpy (&processed->format[0], &(uint16_t){ SENSOR_COLUMNS }, sizeof (uint16_t));
  memcpy (&processed->format[2], &(uint16_t){ SENSOR_ROWS }, sizeof (uint16_t));
  processed->capacity = PROCESSED_FRAME_SIZE;
  processed->valid = 1;
  processed->purpose = purpose;
  memset (processed_data, 0, PROCESSED_FRAME_SIZE);
  quality_coverage[0] = 0;
  quality_coverage[1] = 0;
  return api->preprocessor (&source, &purpose, api->auxiliary, processed,
                            quality_coverage, 0, 0);
}

static int
parse_prelude_argument (const wchar_t *argument, int32_t *purpose,
                        const wchar_t **path)
{
  if (!argument || (argument[0] != L'0' && argument[0] != L'1') ||
      argument[1] != L':' || argument[2] == L'\0')
    return 0;
  *purpose = argument[0] - L'0';
  *path = argument + 2;
  return 1;
}

static int
pack_template (oracle_api *api, void *object, packed_template *packed)
{
  int32_t size = api->template_get_packed_size (object);

  memset (packed, 0, sizeof (*packed));
  if (size <= 0 || size > MAX_TEMPLATE_SIZE)
    return 0;
  packed->data = malloc ((size_t) size);
  if (!packed->data || api->template_pack (object, packed->data) != 0)
    {
      free (packed->data);
      packed->data = NULL;
      return 0;
    }
  packed->size = size;
  return 1;
}

static void
write_u32 (uint8_t *output, uint32_t value)
{
  memcpy (output, &value, sizeof (value));
}

static uint8_t *
pack_tagged_u32 (uint8_t *output, uint8_t tag, uint32_t value)
{
  *output++ = tag;
  write_u32 (output, value);
  return output + sizeof (value);
}

static uint8_t *
pack_bitmap (uint8_t *output, uint8_t tag, const uint8_t bitmap[286])
{
  *output++ = tag;
  write_u32 (output, 286 + 25);
  output += 4;
  output = pack_tagged_u32 (output, 0xc1, 52);
  output = pack_tagged_u32 (output, 0xc2, 44);
  output = pack_tagged_u32 (output, 0xc3, UINT32_MAX);
  output = pack_tagged_u32 (output, 0xc4, 8);
  *output++ = 0xc5;
  write_u32 (output, 286);
  output += 4;
  memcpy (output, bitmap, 286);
  return output + 286;
}

static uint32_t
template_crc32 (const uint8_t *data, size_t size)
{
  uint32_t crc = UINT32_MAX;

  for (size_t i = 0; i < size; i++)
    {
      crc ^= data[i];
      for (size_t bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^
              (0xedb88320U & (uint32_t) -(int32_t) (crc & 1));
    }
  return ~crc;
}

static int
serialize_identify_probe (const void *probe, packed_template *packed,
                          int32_t *record_count_out,
                          int32_t *partition0_count_out)
{
  static const size_t bitmap_offsets[3] = { 0x08, 0x10, 0x18 };
  static const uint8_t bitmap_tags[3] = { 0xb2, 0xcf, 0xcd };
  static const size_t field_offsets[11] = {
    0x110, 0x114, 0x118, 0x11c, 0x120, 0x124,
    0x128, 0x12c, 0x130, 0x134, 0x14c,
  };
  static const uint8_t field_tags[11] = {
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xc0,
  };
  static const uint8_t header_tags[13] = {
    0x81, 0x98, 0x9a, 0x9b, 0x91, 0x97, 0x92,
    0x9e, 0x9f, 0x9c, 0x9d, 0xfa, 0xfb,
  };
  static const uint32_t header_values[13] = {
    0x11f248ea, 12, 88, 104, 1, 1, 1, 150, 150, 1, 1, 0, 0,
  };
  static const uint8_t descriptor_swap[8] = { 0, 1, 1, 0, 1, 0, 0, 1 };
  static const char version[] = "Milan_v_3.01.09.10.50";
  const uint8_t *feature = probe;
  const uint8_t *records;
  const uint8_t *antifake;
  const uint8_t *bitmaps[3];
  uint8_t tail[TEMPLATE_TAIL_SIZE];
  int32_t record_count;
  int32_t partition0_count;
  int32_t optional_c7;
  size_t feature_size;
  size_t total_size;
  uint8_t *output;

  memset (packed, 0, sizeof (*packed));
  if (!probe || !record_count_out || !partition0_count_out)
    return 0;
  memcpy (&record_count, feature + 0xf0, sizeof (record_count));
  memcpy (&records, feature + 0xf8, sizeof (records));
  memcpy (&antifake, feature + 0x160, sizeof (antifake));
  memcpy (&optional_c7, feature + 0x150, sizeof (optional_c7));
  memcpy (&partition0_count, feature + 0x118, sizeof (partition0_count));
  if (record_count < 0 || record_count > FEATURE_RECORD_LIMIT ||
      partition0_count < 0 || partition0_count > record_count ||
      !records || !antifake)
    return 0;
  for (size_t i = 0; i < 3; i++)
    {
      const matrix_header *bitmap;

      memcpy (&bitmap, feature + bitmap_offsets[i], sizeof (bitmap));
      if (!bitmap || !bitmap->data || bitmap->size != 286)
        return 0;
      bitmaps[i] = bitmap->data;
    }

  feature_size = 7945 + (size_t) record_count * 32 +
                 (optional_c7 != 0 ? 5 : 0);
  total_size = feature_size + 1433;
  if (total_size > INT32_MAX)
    return 0;
  packed->data = malloc (total_size);
  if (!packed->data)
    return 0;
  packed->size = (int32_t) total_size;

  packed->data[0] = 0x87;
  packed->data[5] = 0x86;
  output = packed->data + 10;
  for (size_t i = 0; i < 13; i++)
    output = pack_tagged_u32 (output, header_tags[i], header_values[i]);
  *output++ = 0x95;
  write_u32 (output, (uint32_t) (feature_size - 5));
  output += 4;
  output = pack_bitmap (output, bitmap_tags[0], bitmaps[0]);
  output = pack_bitmap (output, bitmap_tags[1], bitmaps[1]);
  *output++ = 0xce;
  write_u32 (output, 72);
  output += 4;
  memcpy (output, feature + 0x28, 72);
  output += 72;
  output = pack_bitmap (output, bitmap_tags[2], bitmaps[2]);
  *output++ = 0xb3;
  write_u32 (output, (uint32_t) record_count);
  output += 4;
  *output++ = 0xbf;
  *output++ = 1;
  write_u32 (output, 0x1abc);
  output += 4;
  memcpy (output, antifake, 0x1abc);
  output += 0x1abc;
  *output++ = 0xb4;
  write_u32 (output, (uint32_t) record_count * 32);
  output += 4;
  for (int32_t i = 0; i < record_count; i++)
    {
      const uint8_t *record = records + (size_t) i * FEATURE_RECORD_SIZE;
      uint8_t *packed_record = output + (size_t) i * 32;
      uint16_t x;
      uint16_t y;
      int16_t orientation;
      uint32_t position;

      memcpy (&x, record + 2, sizeof (x));
      memcpy (&y, record + 4, sizeof (y));
      memcpy (&orientation, record + 6, sizeof (orientation));
      position = (((uint32_t) x << 12) | y) << 4 |
                 (orientation < 0
                    ? (uint8_t) ((-(int32_t) orientation >> 8) + 0x80)
                    : (uint8_t) (orientation >> 8));
      memcpy (packed_record, &position, sizeof (position));
      for (size_t j = 0; j < 8; j++)
        {
          uint8_t first = record[16 + j];
          uint8_t second = record[24 + j];
          uint8_t high_first = ((first ^ second) & 0x0f) ^ first;
          uint8_t high_second = ((first ^ second) & 0x0f) ^ second;

          packed_record[4 + j * 2] =
            descriptor_swap[j] == 0 ? high_second : high_first;
          packed_record[5 + j * 2] =
            descriptor_swap[j] == 0 ? high_first : high_second;
        }
      memcpy (packed_record + 20, record + 32, 4);
      memcpy (packed_record + 24, record + 40, 8);
    }
  output += (size_t) record_count * 32;
  for (size_t i = 0; i < 11; i++)
    {
      uint32_t value;

      memcpy (&value, feature + field_offsets[i], sizeof (value));
      output = pack_tagged_u32 (output, field_tags[i], value);
    }
  if (optional_c7 != 0)
    output = pack_tagged_u32 (output, 0xc7, (uint32_t) optional_c7);

  output = pack_tagged_u32 (output, 0x93, 20);
  output = pack_tagged_u32 (output, 0xf2, UINT32_MAX);
  output = pack_tagged_u32 (output, 0xf3, UINT32_MAX);
  output = pack_tagged_u32 (output, 0xf4, UINT32_MAX);
  output = pack_tagged_u32 (output, 0xf5, 0);
  *output++ = 0x94;
  write_u32 (output, 0x530);
  output += 4;

  memset (tail, 0, sizeof (tail));
  memset (tail, 0xff, 200);
  memset (tail, 0, sizeof (uint32_t));
  memset (tail + 0xc8, 0xff, sizeof (uint32_t));
  memcpy (tail + 0xcc, version, sizeof (version));
  *output++ = 0xa1;
  write_u32 (output, 200);
  output += 4;
  memcpy (output, tail, 200);
  output += 200;
  uint32_t value;
  memcpy (&value, tail + 0xc8, sizeof (value));
  output = pack_tagged_u32 (output, 0xa2, value);
  *output++ = 0xa3;
  write_u32 (output, 64);
  output += 4;
  memcpy (output, tail + 0xcc, 64);
  output += 64;
  *output++ = 0xa4;
  write_u32 (output, 0x400);
  output += 4;
  memcpy (output, tail + 0x10c, 0x400);
  output += 0x400;
  for (size_t i = 0; i < 4; i++)
    {
      memcpy (&value, tail + 0x50c + i * 4, sizeof (value));
      output = pack_tagged_u32 (output, (uint8_t) (0xa5 + i), value);
    }
  if ((size_t) (output - packed->data) != total_size)
    {
      free (packed->data);
      memset (packed, 0, sizeof (*packed));
      return 0;
    }
  write_u32 (packed->data + 6, (uint32_t) (total_size - 10));
  write_u32 (packed->data + 1,
             template_crc32 (packed->data + 10, total_size - 10));
  *record_count_out = record_count;
  *partition0_count_out = partition0_count;
  return 1;
}

static int
queue_occupancy (const int32_t ranks[QUEUE_CAPACITY])
{
  int occupied = 0;

  for (int i = 0; i < QUEUE_CAPACITY; i++)
    {
      if (ranks[i] < -1 || ranks[i] >= QUEUE_CAPACITY)
        return -1;
      occupied += ranks[i] >= 0;
    }
  return occupied;
}

static int
write_code_byte (uint8_t *address, uint8_t value)
{
  DWORD old_protection;
  DWORD ignored;

  if (!VirtualProtect (address, 1, PAGE_EXECUTE_READWRITE, &old_protection))
    return 0;
  *address = value;
  FlushInstructionCache (GetCurrentProcess (), address, 1);
  return VirtualProtect (address, 1, old_protection, &ignored) != 0;
}

static int
install_zero_shadow (uint8_t *feature)
{
  matrix_header *original;
  matrix_header *shadow;
  int32_t record_count;

  if (!feature || !boundary_malloc || !boundary_free)
    return 0;
  original = *(matrix_header **) (feature + FEATURE_MATRIX_OFFSET);
  record_count = *(int32_t *) (feature + 0xf0);
  if (!original || original->width != 52 || original->height != 44 ||
      original->stride != 52 || original->size != FEATURE_MATRIX_BYTES ||
      original->element_size != 1 ||
      original->data != (uint8_t *) original + 0x20 ||
      record_count < 0 || record_count > FEATURE_RECORD_LIMIT ||
      (record_count && !*(void **) (feature + 0xf8)))
    return 0;
  shadow = boundary_malloc (FEATURE_MATRIX_SHADOW_ALLOCATION);
  if (!shadow)
    return 0;
  memcpy (shadow, original, FEATURE_MATRIX_ALLOCATION);
  shadow->data = (uint8_t *) shadow + 0x20;
  shadow->data[FEATURE_MATRIX_BYTES] = 0;
  *(matrix_header **) (feature + FEATURE_MATRIX_OFFSET) = shadow;
  boundary_free (original);
  boundary_record_count = record_count;
  return 1;
}

static LONG CALLBACK
boundary_handler (PEXCEPTION_POINTERS exception)
{
  CONTEXT *context = exception->ContextRecord;

  if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
      boundary_rearm_site)
    {
      if (!write_code_byte (boundary_rearm_site, 0xcc))
        return EXCEPTION_CONTINUE_SEARCH;
      context->EFlags &= ~0x100U;
      boundary_rearm_site = NULL;
      return EXCEPTION_CONTINUE_EXECUTION;
    }
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT ||
      exception->ExceptionRecord->ExceptionAddress != boundary_site)
    return EXCEPTION_CONTINUE_SEARCH;
  boundary_hook_hits++;
  if (boundary_hook_hits != 1 ||
      !install_zero_shadow (*(uint8_t **) ((uint8_t *) boundary_module +
                                           PROBE_GLOBAL_RVA)))
    boundary_error = 1;
  if (!write_code_byte (boundary_site, boundary_saved_byte))
    return EXCEPTION_CONTINUE_SEARCH;
  context->EFlags |= 0x100U;
  boundary_rearm_site = boundary_site;
  context->Rip = (DWORD64) (uintptr_t) boundary_site;
  return EXCEPTION_CONTINUE_EXECUTION;
}

static int
run_boundary (const wchar_t *dll_path, const wchar_t *output_directory,
               const wchar_t *base_path, const wchar_t *live_path,
               const wchar_t *template_path,
               const wchar_t *const *prelude_arguments,
               int prelude_count)
{
  oracle_api api;
  uint16_t *base = NULL;
  uint16_t *live = NULL;
  uint16_t *prelude = NULL;
  uint8_t *template_input = NULL;
  size_t template_input_size = 0;
  void *template_object = NULL;
  void *candidate_data;
  void *candidate;
  void *candidates[1];
  uint8_t processed_data[PROCESSED_FRAME_SIZE];
  image_descriptor processed;
  int32_t preprocess_quality[2] = { 0 };
  int32_t preprocess_status = -1;
  uint32_t identify_quality[2] = { 0 };
  uint32_t matched_index = UINT32_MAX;
  int32_t score = 0;
  int32_t identify_status = -1;
  int32_t study_status = -1;
  int32_t study_update = 0;
  int32_t exit_status = -1;
  packed_template after_match = { 0 };
  packed_template after_study = { 0 };
  packed_template probe_template = { 0 };
  const uint8_t *next_persistent_data;
  size_t next_persistent_size;
  feature_delete_fn feature_delete = NULL;
  PVOID handler = NULL;
  void *malloc_address;
  void *free_address;
  char result[4096];
  int matched;
  int persistence_advanced;
  int probe_record_count = -1;
  int probe_partition0_count = -1;
  int queue_before_match = -1;
  int queue_after_match = -1;
  int queue_after_study = -1;
  int preprocess_attempted = 0;
  int preprocess_completed = 0;
  int extraction_attempted = 0;
  int extraction_completed = 0;
  int study_attempted = 0;
  int study_completed = 0;
  int ok = 0;

  memset (&api, 0, sizeof (api));
  boundary_module = NULL;
  boundary_site = NULL;
  boundary_rearm_site = NULL;
  boundary_malloc = NULL;
  boundary_free = NULL;
  boundary_hook_hits = 0;
  boundary_error = 0;
  boundary_record_count = -1;

  base = read_frame (base_path);
  live = read_frame (live_path);
  template_input = read_file (template_path, 0, &template_input_size);
  if (!base || !live || !template_input || template_input_size > INT32_MAX ||
      template_input_size > MAX_TEMPLATE_SIZE)
    goto out;
  if (!open_api (&api, dll_path) || !start_generation (&api, base))
    goto out;
  for (int i = 0; i < prelude_count; i++)
    {
      int32_t prelude_status;
      int32_t prelude_purpose;
      const wchar_t *prelude_path;

      if (!parse_prelude_argument (prelude_arguments[i], &prelude_purpose,
                                   &prelude_path))
        goto out;
      prelude = read_frame (prelude_path);
      if (!prelude)
        goto out;
      prelude_status = preprocess_frame (
        &api, prelude, processed_data, &processed, preprocess_quality,
        prelude_purpose);
      if (prelude_status != 0 &&
          prelude_status != PREPROCESS_RETRY_RAW_ADMISSION &&
          prelude_status != PREPROCESS_RETRY_VALIDATION_B &&
          prelude_status != PREPROCESS_RETRY_STATEFUL &&
          prelude_status != PREPROCESS_RETRY_CLASSIFICATION)
        goto out;
      free (prelude);
      prelude = NULL;
    }
  if (api.template_unpack (template_input, (int32_t) template_input_size, NULL,
                           &template_object) != 0 || !template_object)
    goto out;
  preprocess_attempted = 1;
  preprocess_status = preprocess_frame (
    &api, live, processed_data, &processed, preprocess_quality,
    PURPOSE_IDENTIFY);
  if (preprocess_status != 0)
    {
      if (preprocess_status != PREPROCESS_RETRY_RAW_ADMISSION &&
          preprocess_status != PREPROCESS_RETRY_VALIDATION_B &&
          preprocess_status != PREPROCESS_RETRY_STATEFUL &&
          preprocess_status != PREPROCESS_RETRY_CLASSIFICATION)
        goto out;
      if (preprocess_status == PREPROCESS_RETRY_CLASSIFICATION &&
          !write_file (output_directory, L"processed.u8", processed_data,
                       sizeof (processed_data)))
        goto out;
      exit_status = api.preprocessor_exit ();
      api.preprocessor_started = 0;
      if (exit_status != 0)
        goto out;
      int length = snprintf (
        result, sizeof (result),
        "{\"schema\":\"milan-parity-native-oracle/v1\","
        "\"profile\":9,\"subtype\":12,\"purpose\":0,"
        "\"anti_fake_mode\":1,\"recognition_mode\":1,"
        "\"normal_extraction\":false,\"normal_antifake\":false,"
        "\"prelude_frames\":%d,"
        "\"execution_mode\":\"natural-identify-study-v1\","
        "\"hook_hits\":0,"
        "\"preprocess\":{\"status\":%ld,\"quality\":%ld,"
        "\"coverage\":%ld},"
        "\"lifecycle\":{"
        "\"extraction\":{\"attempted\":false,\"completed\":false},"
        "\"preprocess\":{\"attempted\":true,\"completed\":false},"
        "\"study\":{\"attempted\":false,\"completed\":false}},"
        "\"preprocessor_exit_status\":%ld}\n",
        prelude_count, (long) preprocess_status,
        (long) preprocess_quality[1], (long) preprocess_quality[0],
        (long) exit_status);
      if (length < 0 || (size_t) length >= sizeof (result) ||
          !write_file (output_directory, L"oracle-result.json", result,
                       (size_t) length))
        goto out;
      ok = 1;
      goto out;
    }
  preprocess_completed = 1;

  boundary_module = api.module;
  boundary_site = (uint8_t *) api.module + IDENTIFY_PRE_ANTIFAKE_RVA;
  boundary_saved_byte = *boundary_site;
  malloc_address = *(void **) ((uint8_t *) api.module + MALLOC_IAT_RVA);
  free_address = *(void **) ((uint8_t *) api.module + FREE_IAT_RVA);
  memcpy (&boundary_malloc, &malloc_address, sizeof (boundary_malloc));
  memcpy (&boundary_free, &free_address, sizeof (boundary_free));
  feature_delete = (feature_delete_fn) (void *)
    ((uint8_t *) api.module + FEATURE_DELETE_RVA);
  if (boundary_saved_byte != 0x4c || !boundary_malloc || !boundary_free ||
      !feature_delete)
    goto out;
  handler = AddVectoredExceptionHandler (1, boundary_handler);
  if (!handler || !write_code_byte (boundary_site, 0xcc))
    goto out;

  candidate_data = *(void **) template_object;
  candidate = &candidate_data;
  candidates[0] = candidate;
  int32_t *ranks = (int32_t *) ((uint8_t *) candidate_data +
                                QUEUE_RANKS_OFFSET);
  queue_before_match = queue_occupancy (ranks);
  if (queue_before_match < 0)
    goto out;
  extraction_attempted = 1;
  identify_status = api.identify_image (
    &processed, api.auxiliary, candidates, 1, &matched_index, &score,
    identify_quality, 0, 1, api.calibration, live, T_CODE, DAC_HIGH, DAC_LOW,
    ANTIFAKE_MODE);
  if (boundary_rearm_site ||
      !write_code_byte (boundary_site, boundary_saved_byte))
    goto out;
  RemoveVectoredExceptionHandler (handler);
  handler = NULL;
  if (boundary_error || boundary_hook_hits != 1 || identify_status != 0 ||
      (matched_index != 0 && matched_index != UINT32_MAX) ||
      ((matched_index == 0) != (score > 0)) ||
      !serialize_identify_probe (
        *(void **) ((uint8_t *) api.module + PROBE_GLOBAL_RVA), &probe_template,
        &probe_record_count, &probe_partition0_count) ||
      boundary_record_count != probe_record_count)
    goto out;
  extraction_completed = 1;
  if (!pack_template (&api, template_object, &after_match) ||
      (queue_after_match = queue_occupancy (ranks)) < 0)
    goto out;
  matched = matched_index == 0;
  if (matched)
    {
      study_attempted = 1;
      study_status = api.template_study (&study_update);
      if (study_status != 0)
        goto out;
      study_completed = 1;
      if (study_update < 0 || study_update > 5)
        goto out;
      queue_after_study = queue_occupancy (ranks);
      if (queue_after_study < 0)
        goto out;
    }
  if (!pack_template (&api, template_object, &after_study))
    goto out;
  persistence_advanced = matched && study_update > 0;
  next_persistent_data = persistence_advanced ? after_study.data : template_input;
  next_persistent_size = persistence_advanced
                           ? (size_t) after_study.size : template_input_size;
  if (!write_file (output_directory, L"processed.u8", processed_data,
                    sizeof (processed_data)) ||
      !write_file (output_directory, L"loaded-before-match.bin", template_input,
                   template_input_size) ||
      !write_file (output_directory, L"after-match.bin", after_match.data,
                   (size_t) after_match.size) ||
      !write_file (output_directory, L"after-study.bin", after_study.data,
                    (size_t) after_study.size) ||
      !write_file (output_directory, L"probe-template.bin", probe_template.data,
                   (size_t) probe_template.size) ||
      !write_file (output_directory, L"next-persistent.bin",
                   next_persistent_data, next_persistent_size))
    goto out;
  exit_status = api.preprocessor_exit ();
  api.preprocessor_started = 0;
  if (exit_status != 0)
    goto out;
  char after_study_occupancy[32];
  char study_action[32];
  if (matched)
    {
      snprintf (after_study_occupancy, sizeof (after_study_occupancy), "%d",
                queue_after_study);
      snprintf (study_action, sizeof (study_action), "%ld",
                (long) study_update);
    }
  else
    {
      memcpy (after_study_occupancy, "null", 5);
      memcpy (study_action, "null", 5);
    }
  int length = snprintf (
    result, sizeof (result),
    "{\"schema\":\"milan-parity-native-oracle/v1\","
    "\"profile\":9,\"subtype\":12,\"purpose\":0,"
    "\"anti_fake_mode\":1,\"recognition_mode\":1,"
    "\"normal_extraction\":true,\"normal_antifake\":true,"
    "\"source_index\":2288,\"sentinel\":0,"
    "\"in_range_bytes_preserved\":true,\"shadow_payload_bytes\":2289,"
    "\"matrix_header_size_field\":2288,\"dll_allocator_owned\":true,"
    "\"normal_destructor_compatible\":true,\"prelude_frames\":%d,"
    "\"execution_mode\":\"natural-identify-study-v1\","
    "\"hook_hits\":%d,"
    "\"active_record_count\":%d,"
    "\"partition0_count\":%d,\"partition1_count\":%d,"
    "\"preprocess\":{\"status\":0,\"quality\":%ld,\"coverage\":%ld},"
    "\"identify\":{\"status\":%ld,\"matched_index\":%lu,"
    "\"score\":%ld,\"quality\":%lu,\"coverage\":%lu},"
    "\"study\":{\"enabled\":true,\"called\":%s,\"status\":%ld,"
    "\"update\":%s},\"queue\":{\"occupied_before_match\":%d,"
    "\"occupied_after_match_before_study\":%d,"
    "\"occupied_after_study\":%s},"
    "\"lifecycle\":{"
    "\"extraction\":{\"attempted\":%s,\"completed\":%s},"
    "\"preprocess\":{\"attempted\":%s,\"completed\":%s},"
    "\"study\":{\"attempted\":%s,\"completed\":%s}},"
    "\"persistence_advanced\":%s,"
    "\"preprocessor_exit_status\":%ld}\n",
    prelude_count, boundary_hook_hits, probe_record_count,
    probe_partition0_count, probe_record_count - probe_partition0_count,
    (long) preprocess_quality[1],
    (long) preprocess_quality[0], (long) identify_status,
    (unsigned long) matched_index, (long) score,
    (unsigned long) identify_quality[1],
    (unsigned long) identify_quality[0], matched ? "true" : "false",
    (long) study_status, study_action, queue_before_match,
    queue_after_match, after_study_occupancy,
    extraction_attempted ? "true" : "false",
    extraction_completed ? "true" : "false",
    preprocess_attempted ? "true" : "false",
    preprocess_completed ? "true" : "false",
    study_attempted ? "true" : "false",
    study_completed ? "true" : "false",
    persistence_advanced ? "true" : "false",
    (long) exit_status);
  if (length < 0 || (size_t) length >= sizeof (result) ||
      !write_file (output_directory, L"oracle-result.json", result,
                   (size_t) length))
    goto out;
  ok = 1;

out:
  if (handler)
    {
      if (boundary_site)
        write_code_byte (boundary_site, boundary_saved_byte);
      RemoveVectoredExceptionHandler (handler);
    }
  if (api.module && feature_delete)
    feature_delete ((void **) ((uint8_t *) api.module + PROBE_GLOBAL_RVA));
  free (after_match.data);
  free (after_study.data);
  free (probe_template.data);
  if (template_object && api.template_delete)
    api.template_delete (template_object);
  free (template_input);
  free (base);
  free (live);
  free (prelude);
  close_api (&api);
  return ok;
}

static char *
read_manifest_line (FILE *input)
{
  size_t capacity = 4096;
  size_t length = 0;
  char *line = malloc (capacity);
  int byte;

  if (!line)
    return NULL;
  while ((byte = fgetc (input)) != EOF && byte != '\n')
    {
      if (length + 1 >= capacity)
        {
          char *grown;

          if (capacity >= 16 * 1024 * 1024)
            {
              free (line);
              return NULL;
            }
          capacity *= 2;
          grown = realloc (line, capacity);
          if (!grown)
            {
              free (line);
              return NULL;
            }
          line = grown;
        }
      line[length++] = (char) byte;
    }
  if (byte == EOF && length == 0)
    {
      free (line);
      return NULL;
    }
  if (length && line[length - 1] == '\r')
    length--;
  line[length] = '\0';
  return line;
}

static wchar_t *
utf8_to_wide (const char *value)
{
  int length;
  wchar_t *wide;

  if (!value || !*value)
    return NULL;
  length = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                                NULL, 0);
  if (length <= 0)
    return NULL;
  wide = malloc ((size_t) length * sizeof (*wide));
  if (!wide ||
      MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                           wide, length) != length)
    {
      free (wide);
      return NULL;
    }
  return wide;
}

static int
run_batch (const wchar_t *dll_path, const wchar_t *manifest_path)
{
  FILE *manifest = _wfopen (manifest_path, L"rb");
  wchar_t executable[32768];
  size_t line_number = 0;
  int ok = 0;

  if (!manifest)
    return 0;
  if (!GetModuleFileNameW (NULL, executable,
                           sizeof (executable) / sizeof (executable[0])))
    {
      fclose (manifest);
      return 0;
    }
  for (;;)
    {
      char *line = read_manifest_line (manifest);
      char *fields[264];
      wchar_t *wide[264] = { 0 };
      const wchar_t *child_argv[272];
      int field_count = 0;

      if (!line)
        {
          ok = feof (manifest) != 0;
          break;
        }
      line_number++;
      fields[field_count++] = line;
      for (char *cursor = line; *cursor; cursor++)
        if (*cursor == '\t')
          {
            *cursor = '\0';
            if (field_count >= (int) (sizeof (fields) / sizeof (fields[0])))
              goto line_out;
            fields[field_count++] = cursor + 1;
          }
      if (field_count < 4)
        goto line_out;
      for (int i = 0; i < field_count; i++)
        {
          wide[i] = utf8_to_wide (fields[i]);
          if (!wide[i])
            goto line_out;
        }
      child_argv[0] = executable;
      child_argv[1] = L"natural";
      child_argv[2] = dll_path;
      child_argv[3] = wide[0];
      child_argv[4] = wide[1];
      child_argv[5] = wide[2];
      child_argv[6] = wide[3];
      for (int i = 4; i < field_count; i++)
        child_argv[i + 3] = wide[i];
      child_argv[field_count + 3] = NULL;
      if (_wspawnv (_P_WAIT, executable, child_argv) != 0)
        goto line_out;
      for (int i = 0; i < field_count; i++)
        free (wide[i]);
      free (line);
      continue;

line_out:
      fwprintf (stderr, L"batch replay failed at line %llu\n",
                (unsigned long long) line_number);
      for (int i = 0; i < field_count; i++)
        free (wide[i]);
      free (line);
      break;
    }
  fclose (manifest);
  return ok;
}

int
wmain (int argc, wchar_t **argv)
{
  setvbuf (stdout, NULL, _IONBF, 0);
  if (argc == 4 && wcscmp (argv[1], L"batch") == 0)
    {
      if (!run_batch (argv[2], argv[3]))
        return 1;
      return 0;
    }
  if (argc < 7 || wcscmp (argv[1], L"natural") != 0)
    {
      fwprintf (stderr,
                 L"usage: %ls natural DLL OUTPUT BASE LIVE TEMPLATE [PURPOSE:PRELUDE...]\n"
                 L"       %ls batch DLL MANIFEST\n",
                argv[0],
                argv[0]);
      return 2;
    }
  for (int i = 7; i < argc; i++)
    {
      int32_t purpose;
      const wchar_t *path;

      if (!parse_prelude_argument (argv[i], &purpose, &path))
        {
          fwprintf (stderr, L"invalid prelude argument: %ls\n", argv[i]);
          return 2;
        }
    }
  if (!run_boundary (argv[2], argv[3], argv[4], argv[5], argv[6],
                      (const wchar_t *const *) &argv[7], argc - 7))
    {
      fwprintf (stderr, L"native oracle failed\n");
      return 1;
    }
  return 0;
}
