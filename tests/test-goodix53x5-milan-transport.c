/*
 * Goodix profile-9 transport/coordinator scheduling contracts.
 * Copyright (C) 2026 goodix53x5-libfprint contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "drivers_api.h"
#include "drivers/goodix53x5/driver-private.h"
#include "drivers/goodix53x5/device/base.h"

static void mock_submit (FpiUsbTransfer *, guint, GCancellable *,
                         FpiUsbTransferCallback, gpointer);
static void capture_setup (FpDevice *, GoodixMilanGeneration *);
static gint64 test_clock_us;
static gint64 G_GNUC_UNUSED
test_monotonic_time (void)
{
  return test_clock_us;
}
static void reply (FpiUsbTransfer *, guint8, guint8, const guint8 *, gsize);
static void boundary_done (FpiSsm *, FpDevice *, GError *);
static GCancellable *action_cancel_token;
static GCancellable *test_action_cancellable (FpDevice *dev)
{
  (void) dev;
  return action_cancel_token;
}

/* Compile the actual owners, replacing USB submission, the monotonic clock,
 * and setup preparation preceding our completed-capture boundary. FpiSsm is not mocked.
 * The executable supplies these owners instead of their archive objects. */
#define fpi_usb_transfer_submit mock_submit
#define fpi_device_get_cancellable test_action_cancellable
#define goodix_milan_generation_prepare_setup capture_setup
#define g_get_monotonic_time test_monotonic_time
#include "drivers/goodix53x5/device/transport.c"
#include "drivers/goodix53x5/device/commands.c"
#include "drivers/goodix53x5/device/scan.c"
static gboolean idle_test_release (void);
static gboolean idle_test_reset (void);
static gboolean idle_test_claim (GError **error);
static gboolean idle_test_usb_close (void);
static void idle_test_open_complete (FpDevice *dev, GError *error);
static void idle_test_close_complete (FpDevice *dev, GError *error);
static FpiSsm *idle_test_reinit_ssm (FpDevice *dev, FpiSsmHandlerCallback handler,
                                    int states, int cleanup, const char *name);
/* Exercise the real driver close, replacing only USB release and the outer
 * action completion (this transport fixture has no libfprint current GTask). */
#define g_usb_device_release_interface(device, interface, flags, error) idle_test_release ()
#define fpi_device_close_complete idle_test_close_complete
#include "drivers/goodix53x5/goodix53x5.c"
#define g_usb_device_reset(device, error) idle_test_reset ()
#define g_usb_device_claim_interface(device, interface, flags, error) idle_test_claim (error)
#define fpi_ssm_new_full idle_test_reinit_ssm
/* The virtual fixture has no USB handle or outer open GTask. Keep the real
 * session SSM and completion owner, replacing only those platform seams. */
#define g_usb_device_close(device, error) ((void) (device), idle_test_usb_close ())
#define fpi_device_open_complete idle_test_open_complete
#define fpi_device_action_is_cancelled(device) g_cancellable_is_cancelled (action_cancel_token)
#define fpi_device_get_usb_device(device) ((GUsbDevice *) NULL)
#include "drivers/goodix53x5/device/session.c"
#undef fpi_device_get_usb_device
#undef fpi_device_action_is_cancelled
#undef fpi_device_open_complete
#undef g_usb_device_close
#undef fpi_ssm_new_full
#undef g_usb_device_claim_interface
#undef g_usb_device_reset
#undef g_usb_device_release_interface
#undef fpi_device_close_complete
#undef goodix_milan_generation_prepare_setup
#undef fpi_usb_transfer_submit
#undef fpi_device_get_cancellable
#undef g_get_monotonic_time

typedef enum {
  EVENT_CANCELLED,
  EVENT_BEFORE_SLEEP_ACK,
  EVENT_COMPLETES_DURING_CANCEL,
  EVENT_BEFORE_ARM_ACK,
  DUPLICATE_SLEEP_BEFORE_EC_ACK,
  DUPLICATE_SLEEP_AFTER_EC_ACK,
  DUPLICATE_ARM_IN_EVENT_WAIT,
  LATE_ACK_DEADLINE,
  CANCEL_DURING_RETRY,
  EARLY_REARM_REVERSE,
  TWO_EARLY_REVERSE,
  SEND_IO_RETRY,
  SEND_TIMEOUT_RETRY,
  SEND_DISCONNECT,
  SEND_CANCELLED,
  SEND_FAILED_RETRY,
  SEND_STALL_RETRY,
  SEND_INTERNAL_RETRY,
  WRITE_STALLED_CANCEL,
  EARLIER_CLEANUP_ERROR,
  CLEANUP_LATE_PROTO,
  SAME_COMMAND_PRECEDENCE,
  MULTICELL_DATA,
  ACK_ZERO_THEN_ONE,
  ACK_TWO_THEN_ONE,
  ACK_EVEN_TIMEOUT,
  ACK_EVEN_THEN_MALFORMED,
  LATE_EVEN_ACK,
  LATE_EVEN_DEADLINE,
  RESPONSE_BUDGET,
  RESPONSE_DEADLINE,
  EC_LATE_ACK,
  EC_LATE_DATA,
  DOWN_BEFORE_SLEEP_ACK,
  REVERSE_BEFORE_SLEEP_ACK,
  STOP_DRAIN_CONTROL,
  CANCEL_ARM_FIRST,
  CANCEL_ARM_REPEAT,
  CANCEL_ARM_CONFIG,
  RESPONSE_EARLY,
  RESPONSE_RETRY,
  RESPONSE_EARLY_RESET,
  RESPONSE_STATUS,
  RESPONSE_DUPLICATE,
  RESPONSE_ASYNC,
  RESPONSE_HANDOFF,
  ARM_STATUS,
  ARM_CONFIG_FAIL,
  ARM_REPEAT_FAIL,
  ARM_REPEAT_STATUS,
  ARM_REFRESH,
  ARM_MAX,
  ARM_CONFIG_DATA_FAIL,
  ARM_CONFIG_PROTO,
  ARM_CONFIG_CANCEL,
  ARM_FIRST_FAIL,
} EventOrder;

typedef struct {
  guint8 timeout_command;
  guint timeout_count;
  guint expected_up_sends;
  guint expected_sleep_sends;
  gboolean success;
  EventOrder event_order;
  guint8 standalone_arm;
  guint8 ec_status;
} Scenario;

static struct {
  FpiUsbTransfer *pending;
  FpiUsbTransferCallback callback;
  gpointer user_data;
  GCancellable *cancel;
  guint timeout;
  guint8 command;
  guint sends[256];
  guint captures;
  guint completions;
  guint cancellations;
  guint cancelled_writes;
  guint started_writes;
  guint precancelled_writes;
  guint events;
  guint dispatches;
  guint duplicates;
  guint expected_acks;
  guint                  even_attempt;
  guint                  even_followups;
  gint64 ec_ack_started;
  gboolean deadline_violation;
  gboolean action_cancelled;
  guint wait_cpu_count;
  guint8 settled_down[GOODIX_FDT_BASE_LEN];
  guint8 settled_manual[GOODIX_FDT_BASE_LEN];
  guint16 settled_prior[GOODIX_PROFILE9_FDT_AREA_COUNT];
  guint8 packet_down[2][GOODIX_FDT_BASE_LEN];
  guint8 packet_manual[2][GOODIX_FDT_BASE_LEN];
  guint8 last_down_payload[GOODIX_FDT_BASE_LEN];
  guint data_chunks;
  guint early_attempt;
  GBytes *first_response;
  guint8 copied_response[4 + GOODIX_FDT_BASE_LEN];
  gboolean disposition_sent;
  gboolean ec_data;
  GError *error;
  GBytes *first_up;
  GBytes *first_sleep;
  GBytes *first_down;
  const Scenario *scenario;
} io;

static void
capture_setup (FpDevice *dev, GoodixMilanGeneration *generation)
{
  g_assert_true (generation == FPI_DEVICE_GOODIX53X5 (dev)->milan_generation);
}

static void
mock_submit (FpiUsbTransfer *transfer, guint timeout, GCancellable *cancel,
             FpiUsbTransferCallback callback, gpointer user_data)
{
  /* Stronger than one IN: no second USB transfer of either direction may
   * outlive an unresolved completion in this serial command/event protocol. */
  g_assert_null (io.pending);
  io.pending = transfer;
  io.callback = callback;
  io.user_data = user_data;
  io.cancel = cancel ? g_object_ref (cancel) : NULL;
  io.timeout = timeout;
  if (transfer->endpoint == GOODIX_EP_IN)
    {
      g_assert_cmpuint (transfer->length, ==, 64);
      if (timeout != 0)
        g_assert_null (cancel);
    }
  else
    {
      GBytes **first = NULL;
      g_assert_cmpuint (transfer->endpoint, ==, GOODIX_EP_OUT);
      if (io.scenario->event_order >= SEND_FAILED_RETRY &&
          io.scenario->event_order <= WRITE_STALLED_CANCEL &&
          (timeout != 0 || cancel != action_cancel_token))
        g_test_fail ();
      if (cancel && g_cancellable_is_cancelled (cancel))
        io.precancelled_writes++;
      else
        io.started_writes++;
      io.command = transfer->buffer[0];
      io.sends[io.command]++;
      io.ec_data = FALSE;
      if (io.scenario->event_order >= ARM_STATUS && io.command == 0x90)
        {
          gsize len;
          const guint8 *config = goodix_device_get_default_config (&len);
          guint8 expected[256];
          guint16 sum = 0xa5a5;

          g_assert_cmpuint (len, ==, sizeof (expected));
          memcpy (expected, config, len);
          expected[0xc7] = 0x80;
          expected[0xc8] = 30; /* Native delta patch; zero TCODE / invalid DAC retain template. */
          for (guint i = 0; i < 254; i += 2)
            sum += expected[i] | ((guint16) expected[i + 1] << 8);
          sum = -sum;
          expected[254] = sum;
          expected[255] = sum >> 8;
          for (guint i = 0; i < sizeof (expected); i++)
            {
              guint offset = i + 3;
              guint wire_offset = offset < 64 ? offset :
                                  64 + ((offset - 64) / 63) * 64 + 1 + (offset - 64) % 63;
              g_assert_cmpuint (transfer->buffer[wire_offset], ==, expected[i]);
            }
        }
      if (io.command == 0xae)
        io.ec_ack_started = 0;
      if (io.command == 0x34)
        first = &io.first_up;
      else if (io.command == 0x60)
        first = &io.first_sleep;
      else if (io.command == 0x32)
        {
          first = &io.first_down;
          memcpy (io.last_down_payload, transfer->buffer + 5, GOODIX_FDT_BASE_LEN);
        }
      else if (io.command == 0x36 || io.command == 0x90)
        first = &io.first_response;
      if (first)
        {
          g_autoptr(GBytes) bytes = g_bytes_new (transfer->buffer,
                                                transfer->length);
          if (*first && !(io.scenario->event_order == ARM_REFRESH && io.command == 0x32) &&
              !(io.command == 0x32 &&
                         (io.scenario->event_order == EARLY_REARM_REVERSE ||
                          io.scenario->event_order == TWO_EARLY_REVERSE) &&
                         io.sends[0x32] > 1 + io.scenario->timeout_count))
            g_assert_true (g_bytes_equal (*first, bytes));
          else if (!*first)
            *first = g_bytes_ref (bytes);
        }
    }
}

static gboolean
rearm_case (const Scenario *scenario)
{
  return scenario->event_order == EARLY_REARM_REVERSE ||
         scenario->event_order == TWO_EARLY_REVERSE;
}

static gboolean
down_drain_case (const Scenario *scenario)
{
  return scenario->event_order == DOWN_BEFORE_SLEEP_ACK ||
         scenario->event_order == REVERSE_BEFORE_SLEEP_ACK;
}

static gboolean
stop_state_case (const Scenario *scenario)
{
  return down_drain_case (scenario) || scenario->event_order == EVENT_BEFORE_SLEEP_ACK ||
         scenario->event_order == STOP_DRAIN_CONTROL;
}

static void
reverse_event (FpiUsbTransfer *transfer, guint ordinal)
{
  guint8 payload[4 + GOODIX_FDT_BASE_LEN] = { 0x80, 0, 0, 0 };
  for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
    payload[4 + 2 * i] = 100 + 2 * i + 2 * ordinal;
  reply (transfer, 3, 1, payload, sizeof (payload));
  io.events++;
}

static void
ack_reply (FpiUsbTransfer *transfer, guint8 command)
{
  guint8 ack[] = { command, 1 };
  reply (transfer, 0x0b, 0, ack, sizeof (ack));
}

static gboolean
polling_case (const Scenario *scenario)
{
  return scenario->event_order >= ACK_ZERO_THEN_ONE &&
         scenario->event_order <= ACK_EVEN_THEN_MALFORMED;
}

/* Independent native caller values, also used by the scheduling assertions. */
static guint
ack_budget (guint8 command)
{
  if (command == 0x60 || command == 0xae)
    return 200;
  if (command == 0x32 || command == 0x34 || command == 0x36 || command == 0x90)
    return 500;
  return 2000;
}

static gboolean
response_case (const Scenario *scenario)
{
  return scenario->event_order == RESPONSE_BUDGET ||
         scenario->event_order == RESPONSE_DEADLINE ||
         scenario->event_order == DUPLICATE_SLEEP_AFTER_EC_ACK ||
         scenario->event_order == EC_LATE_ACK ||
          scenario->event_order == EC_LATE_DATA ||
          (scenario->event_order >= RESPONSE_EARLY && scenario->event_order < ARM_STATUS);
}

static void
reply (FpiUsbTransfer *transfer, guint8 category, guint8 command,
       const guint8 *payload, gsize length)
{
  gsize size;
  g_autofree guint8 *bytes = goodix_proto_build_message (
    category, command, payload, length, TRUE, &size);
  g_assert_cmpuint (size, <=, transfer->length);
  memset (transfer->buffer, 0, transfer->length);
  memcpy (transfer->buffer, bytes, size);
  transfer->actual_length = transfer->length; /* One padded 64-byte protocol cell. */
}

static void
up_event (FpiUsbTransfer *transfer)
{
  guint8 payload[4 + GOODIX_FDT_BASE_LEN] = { 0, 2 };
  for (guint i = 0; i < GOODIX_FDT_BASE_LEN; i++)
    payload[4 + i] = 3 * i + 1;
  reply (transfer, 3, 2, payload, sizeof (payload));
  io.events++;
}

static void
stop_after_capture (FpDevice *dev)
{
  g_assert_cmpuint (io.captures, ==, 1);
  g_assert_false (io.disposition_sent);
  io.disposition_sent = TRUE;
  goodix_scan_set_disposition (dev, GOODIX_SCAN_DISPOSITION_AUTH_SUCCESS, NULL);
}

static gboolean
cancel_arm_case (const Scenario *scenario)
{
  return scenario->event_order >= CANCEL_ARM_FIRST &&
         scenario->event_order <= CANCEL_ARM_CONFIG;
}

static gboolean
complete_usb (gpointer unused)
{
  FpiUsbTransfer *transfer = io.pending;
  FpiUsbTransferCallback callback = io.callback;
  gpointer user_data = io.user_data;
  g_autoptr(GCancellable) cancel = g_steal_pointer (&io.cancel);
  GError *error = NULL;
  const Scenario *scenario = io.scenario;
  guint events_before = io.events;

  if (down_drain_case (scenario) && io.command == 0x32 &&
      transfer->endpoint == GOODIX_EP_IN && !io.disposition_sent)
    stop_after_capture (transfer->device);

  g_assert_nonnull (transfer);
  /* Request stop with the up-arm ACK still outstanding, except for the
   * separately controlled event-completion/cancellation boundary. */
  if (!scenario->standalone_arm && !rearm_case (scenario) && !io.disposition_sent &&
      !cancel_arm_case (scenario) &&
      scenario->event_order != RESPONSE_HANDOFF &&
      scenario->event_order != EVENT_BEFORE_ARM_ACK &&
      transfer->endpoint == GOODIX_EP_IN &&
      io.command == 0x34 &&
      (scenario->event_order != EVENT_COMPLETES_DURING_CANCEL || io.timeout == 0))
    stop_after_capture (transfer->device);
  if (!io.disposition_sent && scenario->event_order == EVENT_BEFORE_ARM_ACK &&
      transfer->endpoint == GOODIX_EP_IN && io.command == 0x32)
    stop_after_capture (transfer->device);

  if (scenario->event_order == CANCEL_DURING_RETRY && io.command == 0x34 &&
      io.sends[0x34] == 2 && !io.action_cancelled)
    {
      io.action_cancelled = TRUE;
      g_cancellable_cancel (FPI_DEVICE_GOODIX53X5 (transfer->device)->cancel);
      g_cancellable_cancel (action_cancel_token);
    }

  if (transfer->ssm == FPI_DEVICE_GOODIX53X5 (transfer->device)->idle_rx_ssm)
    {
      g_assert_nonnull (cancel);
      g_assert_true (g_cancellable_is_cancelled (cancel));
      error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "Scheduled idle receive join");
    }
  else if (transfer->endpoint == GOODIX_EP_OUT)
    {
      EventOrder order = scenario->event_order;
      if (order == WRITE_STALLED_CANCEL && io.command == 0x34 && !io.action_cancelled)
        {
          /* The write stays owned until its cancellation completion. */
          g_assert_cmpuint (io.completions, ==, 0);
          io.action_cancelled = TRUE;
          stop_after_capture (transfer->device);
          g_cancellable_cancel (action_cancel_token);
          g_cancellable_cancel (FPI_DEVICE_GOODIX53X5 (transfer->device)->cancel);
        }
      /* ACK budgets begin after write completion, not at submission. */
      if (response_case (scenario))
        test_clock_us += 50 * 1000;
      if (cancel && g_cancellable_is_cancelled (cancel))
        {
          io.cancelled_writes++;
          error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                       "Action write cancelled before completion");
        }
      else if (io.command == 0x34 && io.sends[0x34] == 1 &&
               ((order >= SEND_IO_RETRY && order <= SEND_INTERNAL_RETRY) ||
                order == EARLIER_CLEANUP_ERROR))
        {
          gint code = order == SEND_IO_RETRY ? G_USB_DEVICE_ERROR_IO :
            order == SEND_TIMEOUT_RETRY ? G_USB_DEVICE_ERROR_TIMED_OUT :
            order == SEND_DISCONNECT || order == EARLIER_CLEANUP_ERROR ? G_USB_DEVICE_ERROR_NO_DEVICE :
            order == SEND_CANCELLED ? G_USB_DEVICE_ERROR_CANCELLED :
            order == SEND_FAILED_RETRY ? G_USB_DEVICE_ERROR_FAILED :
            order == SEND_STALL_RETRY ? G_USB_DEVICE_ERROR_NOT_SUPPORTED : G_USB_DEVICE_ERROR_INTERNAL;
          stop_after_capture (transfer->device);
          error = g_error_new_literal (G_USB_DEVICE_ERROR, code, "Scheduled send failure");
        }
      else
        transfer->actual_length = transfer->length;
    }
  else if (io.timeout == 0)
    {
      if (scenario->standalone_arm)
        {
          if (io.duplicates == 0)
            {
              io.duplicates++;
              ack_reply (transfer, scenario->standalone_arm);
            }
          else
            {
              guint8 payload[4 + GOODIX_FDT_BASE_LEN] = { 0 };
              guint16 irq = scenario->standalone_arm == 0x32 ? 2 : 0x200;
              payload[0] = irq & 0xff;
              payload[1] = irq >> 8;
              for (guint i = 0; i < GOODIX_FDT_BASE_LEN; i++)
                payload[4 + i] = 3 * i + 1;
              reply (transfer, 3, (scenario->standalone_arm & 0xf) >> 1,
                      payload, sizeof (payload));
              io.events++;
            }
        }
      else if (scenario->event_order == EVENT_COMPLETES_DURING_CANCEL && io.events == 0)
        up_event (transfer);
      else
        {
          g_assert_nonnull (cancel);
          g_assert_true (g_cancellable_is_cancelled (cancel));
          io.cancellations++;
          error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                        "Scheduled event cancellation");
        }
    }
  else if (cancel_arm_case (scenario))
    {
      gboolean config = io.command == 0x90;
      gboolean cancel_now = !io.action_cancelled &&
        ((io.command == 0x34 &&
          (scenario->event_order == CANCEL_ARM_FIRST ||
           (scenario->event_order == CANCEL_ARM_REPEAT && io.sends[0x90]))) ||
         (config && io.ec_data && scenario->event_order == CANCEL_ARM_CONFIG));

      if (cancel_now)
        {
          io.action_cancelled = TRUE;
          g_cancellable_cancel (FPI_DEVICE_GOODIX53X5 (transfer->device)->cancel);
        }
      if (io.command == 0x60 && scenario->event_order != CANCEL_ARM_CONFIG && !io.events)
        up_event (transfer);
      else if (config && io.ec_data)
        {
          guint8 status = 0;
          reply (transfer, 9, 0, &status, 1);
        }
      else
        {
          guint8 ack[] = { io.command, io.command == 0x34 && !io.sends[0x90] ?
                          scenario->ec_status : 1 };
          reply (transfer, 0xb, 0, ack, sizeof (ack));
          io.ec_data = config;
        }
    }
  else if (scenario->event_order >= ARM_STATUS)
    {
      gboolean config = io.command == 0x90;
      gboolean repeated = io.sends[0x90] != 0;
      if (config && scenario->event_order == ARM_CONFIG_PROTO)
        reply (transfer, 0xb, 0, &io.command, 1);
      else if (config && scenario->event_order == ARM_CONFIG_CANCEL)
        {
          io.action_cancelled = TRUE;
          g_cancellable_cancel (FPI_DEVICE_GOODIX53X5 (transfer->device)->cancel);
          g_cancellable_cancel (action_cancel_token);
          error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Scheduled repair cancellation");
        }
      else if (scenario->event_order == ARM_FIRST_FAIL ||
               (config && (scenario->event_order == ARM_CONFIG_FAIL ||
                           (scenario->event_order == ARM_CONFIG_DATA_FAIL && io.ec_data))) ||
          (!config && repeated && scenario->event_order == ARM_REPEAT_FAIL) ||
          (!config && scenario->event_order == ARM_MAX && (io.sends[io.command] & 1)))
        {
          test_clock_us += io.timeout * 1000;
          error = g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                       "Scheduled arm recovery timeout");
        }
      else if (config && scenario->event_order == ARM_REFRESH && io.events < 2)
        reverse_event (transfer, io.events);
      else if (config && io.ec_data)
        {
          guint8 status = 0;
          reply (transfer, 9, 0, &status, 1);
        }
      else
        {
          guint8 ack[] = { io.command, config ? 1 :
                          repeated ? (scenario->event_order == ARM_REPEAT_STATUS ? 3 : 1) :
                          scenario->ec_status };
          reply (transfer, 0xb, 0, ack, sizeof (ack));
          io.ec_data = config;
        }
    }
  else if (scenario->event_order == RESPONSE_DUPLICATE && io.command == 0xae &&
           io.sends[0xae] == 2 && io.duplicates < 2)
    {
      if (io.duplicates++ == 0)
        ack_reply (transfer, scenario->standalone_arm);
      else
        {
          guint8 payload[4 + GOODIX_FDT_BASE_LEN];
          memset (payload, 0x77, sizeof (payload));
          reply (transfer, scenario->standalone_arm >> 4,
                 (scenario->standalone_arm & 0xf) >> 1,
                 payload, scenario->standalone_arm == 0x90 ? 1 : sizeof (payload));
        }
      test_clock_us += 75 * 1000;
    }
  else if (scenario->event_order >= RESPONSE_EARLY &&
           io.command == (scenario->event_order == RESPONSE_HANDOFF ?
                          scenario->timeout_command : scenario->standalone_arm))
    {
      guint8 payload[4 + GOODIX_FDT_BASE_LEN];
      gboolean early = scenario->event_order == RESPONSE_EARLY ||
                       (scenario->event_order == RESPONSE_EARLY_RESET && io.sends[io.command] == 1);

      for (guint i = 0; i < sizeof (payload); i++)
        payload[i] = i + 1;
      if (io.command == 0x90)
        payload[0] = scenario->ec_status;
      if ((scenario->event_order == RESPONSE_ASYNC ||
           scenario->event_order == RESPONSE_HANDOFF) && io.events < 2)
        reverse_event (transfer, io.events);
      else if (early && !io.ec_data && io.early_attempt != io.sends[io.command])
        {
          io.early_attempt = io.sends[io.command];
          reply (transfer, io.command >> 4, (io.command & 0xf) >> 1,
                 payload, io.command == 0x90 ? 1 : sizeof (payload));
          test_clock_us += 350 * 1000;
          io.data_chunks++;
        }
      else if ((!io.ec_data && scenario->event_order == RESPONSE_EARLY_RESET &&
                io.sends[io.command] == 1) ||
               (io.ec_data && scenario->event_order == RESPONSE_RETRY &&
                io.sends[io.command] == 1))
        {
          test_clock_us += io.timeout * 1000;
          error = g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                       "Scheduled response-command timeout");
        }
      else if (!io.ec_data)
        {
          if (io.timeout != (early ? 150 : 500))
            g_test_fail ();
          ack_reply (transfer, io.command);
          io.ec_data = TRUE;
        }
      else
        {
          /* An early response must avoid this receive entirely. */
          if (early)
            g_test_fail ();
          reply (transfer, io.command >> 4, (io.command & 0xf) >> 1,
                 payload, io.command == 0x90 ? 1 : sizeof (payload));
          io.data_chunks++;
        }
    }
  else if (!io.ec_data)
    {
      if (scenario->event_order == CLEANUP_LATE_PROTO && io.command == 0xae)
        reply (transfer, 0xb, 0, &io.command, 1);
      else if (scenario->event_order == EC_LATE_ACK && io.command == scenario->standalone_arm &&
          io.duplicates == 0)
        {
          if (io.timeout != 500)
            g_test_fail ();
          test_clock_us += 350 * 1000;
          reply (transfer, 0xa, 7, &scenario->ec_status, 1);
          io.duplicates++;
        }
      else if (polling_case (scenario) && io.command == scenario->timeout_command)
        {
          if (io.even_attempt != io.sends[io.command])
            {
              guint8 ack[] = { io.command, scenario->event_order == ACK_ZERO_THEN_ONE ? 0 : 2 };

              io.even_attempt = io.sends[io.command];
              if (io.timeout != ack_budget (io.command))
                g_test_fail ();
              test_clock_us += ack_budget (io.command) * 750;
              reply (transfer, 0x0b, 0, ack, sizeof (ack));
            }
          else
            {
              io.even_followups++;
              /* The even ACK consumed three quarters of the native budget. */
              if (io.timeout != ack_budget (io.command) / 4)
                g_test_fail ();
              if (scenario->event_order == ACK_EVEN_THEN_MALFORMED)
                {
                  reply (transfer, 0x0b, 0, &io.command, 1);
                }
              else if (io.sends[io.command] <= scenario->timeout_count)
                {
                  test_clock_us += io.timeout * 1000;
                  error = g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                               "Even ACK exhausted attempt deadline");
                }
              else
                {
                  ack_reply (transfer, io.command);
                  io.ec_data = io.command == 0xae;
                }
            }
        }
      else if (rearm_case (scenario) && io.command == 0x32 && io.sends[0x32] == 1 &&
               io.events < (scenario->event_order == TWO_EARLY_REVERSE ? 2 : 1))
        {
          reverse_event (transfer, io.events);
        }
      else if (io.command == scenario->timeout_command &&
          io.sends[io.command] <= scenario->timeout_count)
        {
          test_clock_us += io.timeout * 1000;
          error = g_error_new_literal (G_USB_DEVICE_ERROR,
                                       G_USB_DEVICE_ERROR_TIMED_OUT,
                                       "Scheduled ACK timeout");
        }
      else if (io.command == 0x60 &&
               scenario->event_order == EVENT_BEFORE_SLEEP_ACK && io.events == 0)
        up_event (transfer);
      else if (io.command == 0x60 && down_drain_case (scenario) && io.events == 0)
        {
          guint8 payload[4 + GOODIX_FDT_BASE_LEN] = { 2, 0, 0xff, 0x0f };

          if (scenario->event_order == REVERSE_BEFORE_SLEEP_ACK)
            payload[0] = 0x80;
          for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
            payload[4 + 2 * i] = 100 + 2 * i;
          reply (transfer, 3, 1, payload, sizeof (payload));
          io.events++;
        }
      else if (io.command == 0x34 && scenario->event_order == EVENT_BEFORE_ARM_ACK &&
               io.events == 0)
        up_event (transfer);
      else if (io.command == 0xae &&
               (scenario->event_order == DUPLICATE_SLEEP_BEFORE_EC_ACK ||
                scenario->event_order == LATE_EVEN_ACK) &&
               io.duplicates == 0)
        {
          io.duplicates++;
          if (scenario->event_order == LATE_EVEN_ACK)
            {
              const guint8 ack[] = { 0x60, 2 };

              test_clock_us += ack_budget (io.command) * 750;
              reply (transfer, 0x0b, 0, ack, sizeof (ack));
            }
          else
            {
              ack_reply (transfer, 0x60);
            }
        }
      else if (io.command == 0xae &&
               (scenario->event_order == LATE_ACK_DEADLINE ||
                scenario->event_order == LATE_EVEN_DEADLINE))
        {
          gint64 elapsed_ms;
          if (!io.ec_ack_started)
            io.ec_ack_started = test_clock_us;
          elapsed_ms = (test_clock_us - io.ec_ack_started) / 1000;
          if (io.timeout == 0 || elapsed_ms + io.timeout > ack_budget (io.command))
            io.deadline_violation = TRUE;
          if (io.duplicates == 0)
            {
              /* Exactly the remaining ACK from the two sleep attempts, not
               * an invented stream of more ACKs than commands sent. */
              test_clock_us += ack_budget (io.command) * 750;
              io.duplicates++;
              const guint8 ack[] = { 0x60, scenario->event_order == LATE_EVEN_DEADLINE ? 0 : 1 };

              reply (transfer, 0x0b, 0, ack, sizeof (ack));
            }
          else
            {
              test_clock_us += io.timeout * 1000;
              error = g_error_new_literal (G_USB_DEVICE_ERROR,
                                           G_USB_DEVICE_ERROR_TIMED_OUT,
                                           "Scheduled ACK deadline exhaustion");
            }
        }
      else
        {
          /* In retry scenarios this models the delayed first attempt's ACK.
           * The later injected duplicate belongs to the second send. The wire
           * has no attempt number; both carry identical command/status bytes. */
          ack_reply (transfer, io.command);
          guint expected_timeout = scenario->event_order == EC_LATE_ACK &&
                                    io.command == scenario->standalone_arm ? 150 : ack_budget (io.command);
          if (scenario->event_order == RESPONSE_DUPLICATE && io.command == 0xae && io.sends[0xae] == 2)
            expected_timeout = 50;
          if (response_case (scenario) && io.timeout != expected_timeout)
              g_test_fail ();
          if (io.command == 0xae)
            {
              if (scenario->event_order == LATE_EVEN_ACK && io.timeout != ack_budget (io.command) / 4)
                g_test_fail ();
              io.expected_acks++;
              if (scenario->event_order == RESPONSE_DUPLICATE && io.sends[0xae] == 2 && io.timeout != 50)
                g_test_fail ();
            }
          io.ec_data = io.command == 0xae || scenario->event_order == MULTICELL_DATA ||
                       (response_case (scenario) && io.command == scenario->standalone_arm);
        }
    }
  else
    {
      g_assert_true (io.ec_data);
      if (io.command == 0xae)
        {
          /* Native supplies no required EC response. A baseline that asks for
           * one exhausts its receive rather than receiving an invented byte. */
          error = g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                       "No EC response exists");
        }
      else if (response_case (scenario))
        {
          if (io.duplicates == 0)
            {
              if (io.timeout != 500)
                g_test_fail ();
              test_clock_us += 350 * 1000;
              io.duplicates++;
              if (scenario->event_order == EC_LATE_DATA)
                reply (transfer, 0xa, 7, &scenario->ec_status, 1);
              else
                ack_reply (transfer, 0x60);
            }
          else
            {
              if (io.timeout != (scenario->event_order == EC_LATE_ACK ||
                                 io.sends[io.command] > 1 ? 500 : 150))
                g_test_fail ();
              io.data_chunks++;
              if (scenario->event_order == RESPONSE_DEADLINE)
                {
                  test_clock_us += io.timeout * 1000;
                  error = g_error_new_literal (G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT,
                                               "Response budget exhausted after late ACK");
                }
              else
                {
                  guint8 payload[4 + GOODIX_FDT_BASE_LEN];

                  for (guint i = 0; i < sizeof (payload); i++)
                    payload[i] = i + 1;
                  reply (transfer, io.command >> 4, (io.command & 0xf) >> 1,
                         payload, io.command == 0x90 ? 1 : sizeof (payload));
                }
            }
        }
      else if (scenario->event_order == MULTICELL_DATA)
        {
          guint8 payload[150];
          gsize size;
          for (guint i = 0; i < sizeof (payload); i++)
            payload[i] = i * 7 + 3;
          g_autofree guint8 *message = goodix_proto_build_message (
            2, 0, payload, sizeof (payload), TRUE, &size);
          /* Ordinary data retains its per-continuation 5-second budget.
           * Three valid cells arrive four seconds apart; no wall-clock wait. */
          if (io.timeout != GOODIX_DATA_TIMEOUT)
            g_test_fail ();
          test_clock_us += MIN (io.timeout, 4000) * 1000;
          if (io.timeout < 4000)
            error = g_error_new_literal (G_USB_DEVICE_ERROR,
                                         G_USB_DEVICE_ERROR_TIMED_OUT,
                                         "Continuation budget shortened");
          else
            {
              gsize offset = io.data_chunks ? 64 + (io.data_chunks - 1) * 63 : 0;
              guint prefix = io.data_chunks ? 1 : 0;
              memset (transfer->buffer, 0, transfer->length);
              if (prefix)
                transfer->buffer[0] = 0x21;
              memcpy (transfer->buffer + prefix, message + offset,
                      MIN (64 - prefix, size - offset));
              transfer->actual_length = 64;
              io.data_chunks++;
            }
        }
      else
        {
          g_assert_not_reached ();
        }
    }

  /* Transfer completion releases physical ownership before callbacks can
   * submit another transfer. GCancellable itself never releases ownership. */
  io.pending = NULL;
  io.callback = NULL;
  io.user_data = NULL;
  callback (transfer, transfer->device, user_data, error);
  if (rearm_case (scenario) && io.events > events_before)
    {
      FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (transfer->device);
      memcpy (io.packet_down[events_before], self->profile9_fdt.base_down, GOODIX_FDT_BASE_LEN);
      memcpy (io.packet_manual[events_before], self->profile9_fdt.base_manual, GOODIX_FDT_BASE_LEN);
    }
  fpi_usb_transfer_unref (transfer);
  *(gboolean *) unused = TRUE;
  return G_SOURCE_REMOVE;
}

static void
capture_ready (FpDevice *dev, gpointer unused)
{
  (void) dev;
  (void) unused;
  io.captures++;
}

static void
boundary_handler (FpiSsm *ssm, FpDevice *dev)
{
  GoodixScanCoordinatorData *data = fpi_ssm_get_data (ssm);
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  gboolean handoff = io.scenario->event_order == RESPONSE_HANDOFF;

  if (fpi_ssm_get_cur_state (ssm) == GOODIX_SCAN_COORD_WAIT_CPU &&
      (rearm_case (io.scenario) || handoff))
    {
      io.wait_cpu_count++;
      memcpy (io.settled_down, self->profile9_fdt.base_down, sizeof (io.settled_down));
      memcpy (io.settled_manual, self->profile9_fdt.base_manual, sizeof (io.settled_manual));
      memcpy (io.settled_prior, data->prior_down, sizeof (io.settled_prior));
      g_assert_false (data->cpu_done);
      g_assert_true (data->cpu_outstanding);
    }
  if (fpi_ssm_get_cur_state (ssm) == GOODIX_SCAN_COORD_DISPATCH_EVENT)
    {
      io.dispatches++;
      if (handoff)
        {
          g_assert_cmpuint (self->profile9_fdt.wait_mode, ==, GOODIX_PROFILE9_FDT_WAIT_UP);
          g_assert_true (data->event_preparsed);
        }
    }
  /* Model the completed-capture boundary after a response command. Both
   * reverse events arrive during that command; the real coordinator then
   * arms UP and consumes the latest DOWN notification, before CPU settlement. */
  if (handoff && fpi_ssm_get_cur_state (ssm) == GOODIX_SCAN_COORD_ENSURE_REFERENCE)
    {
      self->profile9_fdt.wait_mode = GOODIX_PROFILE9_FDT_WAIT_DOWN;
      if (io.scenario->timeout_command == 0x36)
        goodix_cmd_fdt_manual (ssm, dev, TRUE, self->profile9_fdt.base_manual);
      else
        {
          gsize len;
          const guint8 *config = goodix_device_get_default_config (&len);
          g_autofree guint8 *patched = g_memdup2 (config, len);

          goodix_device_patch_config (patched, len, &self->calib);
          goodix_cmd_upload_config (ssm, dev, patched, len);
        }
      return;
    }
  if (handoff && fpi_ssm_get_cur_state (ssm) == GOODIX_SCAN_COORD_POWER_ON)
    {
      fpi_ssm_jump_to_state (ssm, GOODIX_SCAN_COORD_ARM_UP);
      return;
    }
  if (fpi_ssm_get_cur_state (ssm) == GOODIX_SCAN_COORD_ENSURE_REFERENCE)
    fpi_ssm_jump_to_state (ssm, (rearm_case (io.scenario) || down_drain_case (io.scenario))
                           ? GOODIX_SCAN_COORD_REARM_DOWN : GOODIX_SCAN_COORD_ARM_UP);
  else
    goodix_scan_coordinator_handler (ssm, dev);
}

/* The two arm-event-wait cases use the public command/receive boundary rather
 * than inventing a second capture cycle to reach a down-arm wait. */
static void
arm_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  gint state = fpi_ssm_get_cur_state (ssm);
  if (io.scenario->event_order == SAME_COMMAND_PRECEDENCE)
    {
      if (state == 1)
        {
          goodix_cmd_fdt_up_setup (ssm, dev, self->profile9_fdt.base_up);
          return;
        }
      if (state > 1)
        state--;
    }
  switch (state)
    {
    case 0:
      self->profile9_fdt.wait_mode = io.scenario->standalone_arm == 0x32
        ? GOODIX_PROFILE9_FDT_WAIT_DOWN : GOODIX_PROFILE9_FDT_WAIT_UP;
      if (io.scenario->standalone_arm == 0x32)
        goodix_cmd_fdt_down_setup (ssm, dev, self->profile9_fdt.base_down);
      else
        goodix_cmd_fdt_up_setup (ssm, dev, self->profile9_fdt.base_up);
      break;
    case 1:
      if (io.scenario->event_order >= ARM_STATUS && goodix_recv_take_pending_fdt (dev))
        fpi_ssm_next_state (ssm);
      else
        goodix_recv_start (ssm, dev, 0, NULL);
      break;
    case 2:
      {
        GoodixFdtEventType type;
        GError *error = NULL;
        if (!goodix_cmd_parse_fdt_event (dev, self->profile9_fdt.wait_mode,
                                         &type, &self->profile9_fdt.event, &error))
          fpi_ssm_mark_failed (ssm, error);
        else
          {
            io.dispatches++;
            fpi_ssm_mark_completed (ssm);
          }
      }
      break;
    }
}

static void
data_handler (FpiSsm *ssm, FpDevice *dev)
{
  /* Opaque multi-cell image-command response at the framing boundary only:
   * these bytes are never submitted to decryption or biometric processing. */
  if (fpi_ssm_get_cur_state (ssm) == 0)
    goodix_cmd_request_image (ssm, dev, TRUE, TRUE, FALSE, 0x80);
  else
    {
      const guint8 *payload;
      gsize length;
      GError *error = NULL;
      if (!goodix_parse_reply_exact (dev, 2, 0, &payload, &length, &error))
        fpi_ssm_mark_failed (ssm, error);
      else
        {
          g_assert_cmpuint (length, ==, 150);
          for (guint i = 0; i < length; i++)
            g_assert_cmpuint (payload[i], ==, (guint8) (i * 7 + 3));
          fpi_ssm_mark_completed (ssm);
        }
    }
}

static void
response_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case 0:
      goodix_cmd_set_sleep_mode (ssm, dev);
      break;

    case 1:
      goodix_cmd_ec_control (ssm, dev, FALSE);
      break;

    case 2:
      if (io.scenario->standalone_arm == 0x36)
        goodix_cmd_fdt_manual (ssm, dev, TRUE, self->profile9_fdt.base_manual);
      else
        {
          gsize len;
          const guint8 *config = goodix_device_get_default_config (&len);
          g_autofree guint8 *patched = g_memdup2 (config, len);

          goodix_device_patch_config (patched, len, &self->calib);
          goodix_cmd_upload_config (ssm, dev, patched, len);
        }
      break;

    case 3:
      {
        const guint8 *payload;
        gsize length;
        g_autoptr(GError) error = NULL;

        if (io.command == 0x90)
          {
            if (!goodix_cmd_parse_config_reply (dev))
              g_test_fail ();
            if (io.scenario->event_order == RESPONSE_DUPLICATE)
              goodix_cmd_ec_control (ssm, dev, FALSE);
            else
              fpi_ssm_mark_completed (ssm);
          }
        else if (!goodix_cmd_parse_fdt_manual_reply (dev, &payload, &length, &error))
          fpi_ssm_mark_failed (ssm, g_steal_pointer (&error));
        else
          {
            g_assert_cmpuint (length, ==, io.command == 0x90 ? 1 : 4 + GOODIX_FDT_BASE_LEN);
            for (guint i = 0; i < length; i++)
              g_assert_cmpuint (payload[i], ==, i + 1);
            memcpy (io.copied_response, payload, length);
            if (io.scenario->event_order == RESPONSE_DUPLICATE)
              goodix_cmd_ec_control (ssm, dev, FALSE);
            else
              fpi_ssm_mark_completed (ssm);
          }
      }
      break;
    case 4:
      if (io.scenario->standalone_arm == 0x36)
        for (guint i = 0; i < sizeof (io.copied_response); i++)
          g_assert_cmpuint (io.copied_response[i], ==, i + 1);
      fpi_ssm_mark_completed (ssm);
      break;
    }
}

static void
arm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  self->profile9_fdt.owner = NULL;
  self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_STOPPED;
  boundary_done (ssm, dev, error);
}

static void
boundary_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  (void) ssm;
  g_assert_null (self->profile9_fdt.owner);
  g_assert_cmpuint (self->profile9_fdt.lifecycle, ==,
                    GOODIX_PROFILE9_FDT_LIFECYCLE_STOPPED);
  io.completions++;
  g_assert_cmpuint (io.completions, ==, 1);
  io.error = error;
}

static void
parent_handler (FpiSsm *ssm, FpDevice *dev)
{
  (void) dev;
  if (fpi_ssm_get_cur_state (ssm) == 0)
    fpi_ssm_start (fpi_ssm_get_data (ssm), goodix_scan_coordinator_done);
  else
    fpi_ssm_mark_completed (ssm);
}

static void
idle_joined (FpDevice *dev, gpointer data)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (dev);
  g_assert_false (self->rx_active);
  g_assert_null (self->idle_rx_ssm);
}

static void
test_scenario (gconstpointer user_data)
{
  const Scenario *scenario = user_data;
  FpDeviceClass *klass = g_type_class_ref (FPI_TYPE_DEVICE_GOODIX53X5);
  g_autoptr(FpDevice) dev = NULL;
  FpiDeviceGoodix53x5 *self;
  GoodixScanCoordinatorData *data;
  FpiSsm *ssm;
  FpiSsm *parent;

  memset (&io, 0, sizeof (io));
  test_clock_us = G_USEC_PER_SEC;
  io.scenario = scenario;
  klass->type = FP_DEVICE_TYPE_VIRTUAL;
  dev = g_object_new (FPI_TYPE_DEVICE_GOODIX53X5, NULL);
  g_type_class_unref (klass);
  self = FPI_DEVICE_GOODIX53X5 (dev);
  self->cancel = g_cancellable_new ();

  /* Explicit abstraction: an admitted frame has already been captured. No
   * preprocessing, extraction, match score or authentication is fabricated.
   * The test owns only the subsequent USB/scan scheduling and final status. */
  self->milan_sensor_subtype = 12;
  self->milan_generation = g_new0 (GoodixMilanGeneration, 1);
  self->captured_raw_image = g_new0 (guint16, GOODIX_SENSOR_PIXELS);
  self->profile9_fdt.base_valid = TRUE;
  self->profile9_fdt.drift_anchor_empty = TRUE;
  self->calib.delta_down = 30;
  action_cancel_token = g_cancellable_new ();
  if (scenario->event_order >= ARM_STATUS)
    io.duplicates = 1; /* These controls need no synthetic late arm ACK. */
  if (stop_state_case (scenario))
    {
      self->profile9_fdt.drift_anchor_empty = FALSE;
      memset (self->profile9_fdt.base_down, 45, GOODIX_FDT_BASE_LEN);
      memset (self->profile9_fdt.base_up, 60, GOODIX_FDT_BASE_LEN);
      memset (self->profile9_fdt.base_manual, 70, GOODIX_FDT_BASE_LEN);
      for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
        {
          self->fdt_prior_down[i] = 0x2222;
          self->profile9_fdt.drift_anchor[i] = 31 + i;
        }
    }
  if (scenario->standalone_arm)
    {
      ssm = scenario->event_order == MULTICELL_DATA
        ? fpi_ssm_new (dev, data_handler, 2)
        : response_case (scenario) ? fpi_ssm_new (dev, response_handler, 5)
        : fpi_ssm_new (dev, arm_handler,
                       scenario->event_order == SAME_COMMAND_PRECEDENCE ? 4 : 3);
      self->profile9_fdt.owner = ssm;
      self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_ACTIVE;
      fpi_ssm_start (ssm, arm_done);
    }
  else
    {
      data = g_new0 (GoodixScanCoordinatorData, 1);
      ssm = fpi_ssm_new_full (dev, boundary_handler, GOODIX_SCAN_COORD_NUM_STATES,
                             GOODIX_SCAN_COORD_CLEANUP_JOIN, "transport-boundary");
      data->ssm = ssm;
      parent = fpi_ssm_new (dev, parent_handler, 2);
      data->parent_ssm = parent;
      fpi_ssm_set_data (parent, ssm, NULL);
      data->capture_ready = capture_ready;
      data->dispatching = TRUE;
      data->action_cancel = g_object_ref (self->cancel);
      if (rearm_case (scenario) || down_drain_case (scenario))
        {
          /* Prior capture and up-release have completed; CPU publication has
           * not. B0 is a programmed base distinct from either early event. */
          data->cycle_active = TRUE;
          data->release_settled = TRUE;
          data->cpu_outstanding = TRUE;
          io.captures = 1;
          memset (self->profile9_fdt.base_down, 45, GOODIX_FDT_BASE_LEN);
        }
      fpi_ssm_set_data (ssm, data, (GDestroyNotify) goodix_scan_coordinator_data_free);
      self->profile9_fdt.owner = ssm;
      self->profile9_fdt.lifecycle = GOODIX_PROFILE9_FDT_LIFECYCLE_ACTIVE;
      fpi_ssm_start (parent, boundary_done);
      if (self->profile9_fdt.owner == ssm)
        data->action_cancel_id = g_cancellable_connect (
          data->action_cancel, G_CALLBACK (goodix_scan_action_cancelled), data, NULL);
    }
  for (guint step = 0; io.completions == 0 && step < 48; step++)
    {
      gboolean completed = FALSE;
      if (!io.pending && cancel_arm_case (scenario) && !io.disposition_sent &&
          self->profile9_fdt.owner &&
          fpi_ssm_get_cur_state (self->profile9_fdt.owner) == GOODIX_SCAN_COORD_WAIT_CPU)
        stop_after_capture (dev);
      if (!io.pending && (rearm_case (scenario) || scenario->event_order == RESPONSE_HANDOFF) &&
          io.wait_cpu_count && !io.disposition_sent)
        stop_after_capture (dev);
      if (!io.pending && !scenario->standalone_arm && self->profile9_fdt.owner &&
          fpi_ssm_get_cur_state (self->profile9_fdt.owner) >= GOODIX_SCAN_COORD_CLEANUP_JOIN &&
          !io.disposition_sent)
        stop_after_capture (dev); /* Join the deferred CPU callback on failure. */
      if (io.completions)
        break;
      if (!io.pending)
        {
          g_assert_true (g_main_context_pending (NULL));
          g_main_context_iteration (NULL, FALSE);
          continue;
        }
      g_idle_add (complete_usb, &completed);
      while (!completed)
        g_main_context_iteration (NULL, TRUE);
    }

  if (self->idle_rx_ssm)
    {
      gboolean completed = FALSE;

      /* Action completion no longer closes the handle-owned idle receiver.
       * Exercise its explicit join before retaining the original owner checks. */
      goodix_idle_recv_stop (dev, idle_joined, NULL);
      g_idle_add (complete_usb, &completed);
      while (!completed)
        g_main_context_iteration (NULL, TRUE);
      g_assert_null (self->idle_rx_ssm);
    }
  g_assert_cmpuint (io.completions, ==, 1);
  g_assert_null (io.pending);
  g_assert_false (self->rx_active);
  g_assert_null (self->rx_owner);
  g_assert_null (self->cmd_owner);
  g_assert_null (self->cmd_ssm);
  g_assert_cmpuint (io.captures, ==, scenario->standalone_arm ? 0 : 1);
  g_test_message ("up=%u sleep=%u cancel=%u events=%u final=%s",
                  io.sends[0x34], io.sends[0x60], io.cancellations, io.events,
                  io.error ? io.error->message : "success");
  /* Report contract differences without aborting, so every scheduled case
   * releases its owners and the complete suite can expose baseline failures. */
  gboolean cancelled = scenario->event_order == CANCEL_DURING_RETRY ||
                       scenario->event_order == WRITE_STALLED_CANCEL ||
                        cancel_arm_case (scenario) ||
                       scenario->event_order == ARM_CONFIG_CANCEL;
  gboolean delivered = (scenario->standalone_arm && scenario->event_order < ARM_STATUS &&
                         scenario->event_order != MULTICELL_DATA &&
                         !response_case (scenario)) ||
    scenario->event_order == EVENT_BEFORE_ARM_ACK ||
    scenario->event_order == EVENT_COMPLETES_DURING_CANCEL;
  gboolean duplicate = (scenario->standalone_arm && scenario->event_order != MULTICELL_DATA &&
                         scenario->event_order < RESPONSE_EARLY) ||
    scenario->event_order == DUPLICATE_SLEEP_BEFORE_EC_ACK ||
    scenario->event_order == DUPLICATE_SLEEP_AFTER_EC_ACK;
  if (rearm_case (scenario))
    {
      gboolean two = scenario->event_order == TWO_EARLY_REVERSE;
      if (io.wait_cpu_count != 1 || io.dispatches != 1 ||
          io.events != (two ? 2 : 1) || io.sends[0x32] != (two ? 3 : 2) ||
          self->profile9_fdt.event.irq != 0x80 ||
          self->profile9_fdt.event.touch_flag != 0 ||
          fp_device_get_finger_status (dev) != FP_FINGER_STATUS_NONE)
        g_test_fail ();
      /* Native transform: ((sample / 2) * 0x101) modulo 16 bits. These
       * chosen samples stay below wrapping; both programmed bytes equal q.
       * R2's prior/manual snapshot must be transform(R1), not original B0. */
      for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
        {
          guint8 prior = two ? 50 + i : 45;
          guint8 current = 50 + i + two;
          if (io.settled_prior[i] != prior ||
              io.settled_manual[2 * i] != prior ||
              io.settled_manual[2 * i + 1] != prior ||
              io.settled_down[2 * i] != current ||
              io.settled_down[2 * i + 1] != current ||
              io.last_down_payload[2 * i] != current ||
              io.last_down_payload[2 * i + 1] != current ||
              self->profile9_fdt.event.raw[2 * i] != 100 + 2 * i + 2 * two ||
              self->profile9_fdt.event.raw[2 * i + 1] != 0)
            g_test_fail ();
          for (guint packet = 0; packet < (two ? 2 : 1); packet++)
            {
              guint8 packet_prior = packet ? 50 + i : 45;
              guint8 packet_current = 50 + i + packet;
              if (io.packet_down[packet][2 * i] != packet_current ||
                  io.packet_down[packet][2 * i + 1] != packet_current ||
                  io.packet_manual[packet][2 * i] != packet_prior ||
                  io.packet_manual[packet][2 * i + 1] != packet_prior)
                g_test_fail ();
            }
        }
      g_test_message ("rearm events=%u dispatch=%u wait_cpu=%u down_sends=%u prior0=%u manual0=%u down0=%u",
                      io.events, io.dispatches, io.wait_cpu_count, io.sends[0x32],
                      io.settled_prior[0], io.settled_manual[0], io.settled_down[0]);
      g_test_message ("packet mutations R1 down/manual=%u/%u R2 down/manual=%u/%u",
                      io.packet_down[0][0], io.packet_manual[0][0],
                      io.packet_down[1][0], io.packet_manual[1][0]);
    }
  gboolean excluded_send = scenario->event_order == SEND_DISCONNECT ||
    scenario->event_order == SEND_CANCELLED || scenario->event_order == EARLIER_CLEANUP_ERROR;
  if (cancel_arm_case (scenario))
    {
      gboolean armed = scenario->event_order != CANCEL_ARM_CONFIG;

      if (io.dispatches || io.events != armed ||
          io.sends[0x90] != (scenario->event_order != CANCEL_ARM_FIRST) ||
          io.sends[0x32] || self->pending_fdt_packet_len)
        g_test_fail ();
      for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
        {
          guint16 sample = (6 * i + 1) | ((6 * i + 4) << 8);
          guint16 expected = armed ? (sample / 2) * 0x101 : 0;

          if (self->profile9_fdt.base_down[2 * i] != (expected & 0xff) ||
              self->profile9_fdt.base_down[2 * i + 1] != (expected >> 8))
            g_test_fail ();
        }
    }
  gboolean malformed = scenario->event_order == ACK_EVEN_THEN_MALFORMED ||
                       scenario->event_order == ARM_CONFIG_PROTO;
  if (stop_state_case (scenario))
    {
      gboolean down = scenario->event_order == DOWN_BEFORE_SLEEP_ACK;
      gboolean reverse = scenario->event_order == REVERSE_BEFORE_SLEEP_ACK;
      gboolean up = scenario->event_order == EVENT_BEFORE_SLEEP_ACK;

      if (io.events != (down || reverse || up) || io.dispatches ||
          io.cancellations != 1 || self->profile9_fdt.event.pending ||
          self->pending_fdt_packet_len || !self->profile9_fdt.base_valid ||
          self->profile9_fdt.drift_anchor_empty)
        g_test_fail ();
      for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
        {
          /* Up uses the existing fixture's consecutive bytes; reverse/down
           * use samples 100,102,... . Expected words are independent formulas. */
          guint16 sample = (6 * i + 1) | ((6 * i + 4) << 8);
          guint16 expected_down = up ? (guint16) ((sample >> 1) * 257) :
                                   reverse ? (50 + i) * 257 : 45 * 257;
          guint16 expected_up = down ? (80 + i) * 257 : 60 * 257;
          guint8 expected_manual = reverse ? 45 : 70;

          if (self->profile9_fdt.base_down[2 * i] != (expected_down & 0xff) ||
              self->profile9_fdt.base_down[2 * i + 1] != (expected_down >> 8) ||
              self->profile9_fdt.base_up[2 * i] != (expected_up & 0xff) ||
              self->profile9_fdt.base_up[2 * i + 1] != (expected_up >> 8) ||
              self->profile9_fdt.base_manual[2 * i] != expected_manual ||
              self->profile9_fdt.base_manual[2 * i + 1] != expected_manual ||
              self->fdt_prior_down[i] != (reverse ? 45 : 0x2222) ||
              self->profile9_fdt.drift_anchor[i] != 31 + i)
            g_test_fail ();
        }
    }
  if (response_case (scenario) && scenario->event_order < RESPONSE_EARLY &&
      (io.sends[scenario->standalone_arm] != (scenario->event_order == RESPONSE_DEADLINE ? 2 : 1) ||
       io.data_chunks != (scenario->event_order == RESPONSE_DEADLINE ? 2 : 1) || io.events || io.dispatches))
    g_test_fail ();
  if (scenario->event_order >= RESPONSE_EARLY && scenario->event_order < ARM_STATUS &&
      scenario->event_order != RESPONSE_HANDOFF &&
      (io.sends[scenario->standalone_arm] !=
       (scenario->event_order == RESPONSE_RETRY || scenario->event_order == RESPONSE_EARLY_RESET ? 2 : 1) ||
       io.events != (scenario->event_order == RESPONSE_ASYNC ? 2 : 0) || io.dispatches))
    g_test_fail ();
  if (scenario->event_order == RESPONSE_EARLY_RESET && io.data_chunks != 2)
    g_test_fail ();
  if (scenario->event_order == RESPONSE_ASYNC)
    for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
      if (self->profile9_fdt.base_down[2 * i] != 51 + i ||
          self->profile9_fdt.base_down[2 * i + 1] != 51 + i ||
          self->profile9_fdt.base_manual[2 * i] != 50 + i ||
          self->profile9_fdt.base_manual[2 * i + 1] != 50 + i ||
          self->fdt_prior_down[i] != 50 + i || !self->pending_fdt_packet_len)
        g_test_fail ();
  if (scenario->event_order == RESPONSE_HANDOFF)
    {
      if (io.events != 2 || io.dispatches != 1 || io.wait_cpu_count != 1 ||
          io.sends[scenario->timeout_command] != 1 || io.sends[0x32] != 1 ||
          self->pending_fdt_packet_len || self->profile9_fdt.event.irq != 0x80)
        g_test_fail ();
      for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
        if (io.settled_prior[i] != 50 + i ||
            io.settled_manual[2 * i] != 50 + i ||
            io.settled_manual[2 * i + 1] != 50 + i ||
            io.settled_down[2 * i] != 51 + i ||
            io.settled_down[2 * i + 1] != 51 + i ||
            io.last_down_payload[2 * i] != 51 + i ||
            io.last_down_payload[2 * i + 1] != 51 + i ||
            self->profile9_fdt.event.raw[2 * i] != 102 + 2 * i ||
            self->profile9_fdt.event.raw[2 * i + 1] != 0)
          g_test_fail ();
      g_test_message ("handoff dispatch=%u wait_cpu=%u prior/manual/down=%u/%u/%u",
                      io.dispatches, io.wait_cpu_count, io.settled_prior[0],
                      io.settled_manual[0], io.settled_down[0]);
    }
  if (polling_case (scenario) &&
      (io.even_followups != io.sends[scenario->timeout_command] ||
       (scenario->timeout_command == 0xae && io.sends[0xae] != 1)))
    g_test_fail ();
  if (scenario->event_order == LATE_EVEN_ACK &&
      (io.duplicates != 1 || io.expected_acks != 1 || io.sends[0xae] != 1))
    g_test_fail ();
  if (malformed && !g_error_matches (io.error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO))
    g_test_fail ();
  if (delivered && io.dispatches == 1)
    for (guint i = 0; i < GOODIX_FDT_BASE_LEN; i++)
      if (self->profile9_fdt.event.raw[i] != 3 * i + 1)
        g_test_fail ();
  gboolean exhausted_arm = scenario->event_order == ARM_REPEAT_FAIL ||
                           scenario->event_order == ARM_FIRST_FAIL;
  if (!scenario->standalone_arm &&
      ((scenario->timeout_command == 0x60 && scenario->timeout_count == 2) ||
       scenario->timeout_command == 0xae))
    {
      gboolean cleanup_only = scenario->event_order != EARLIER_CLEANUP_ERROR &&
                              scenario->event_order != CLEANUP_LATE_PROTO;
      if (self->scan_cleanup_only_error != cleanup_only)
        g_test_fail ();
    }
  if ((cancelled || excluded_send) && self->scan_cleanup_only_error)
    g_test_fail ();
  if (scenario->event_order == WRITE_STALLED_CANCEL &&
      (io.started_writes != 1 || io.precancelled_writes != 2 || io.cancelled_writes != 3))
    g_test_fail ();
  if (scenario->event_order >= ARM_STATUS)
    {
      guint arms = malformed || cancelled ? 1 : scenario->event_order == ARM_MAX ? 4 :
                   scenario->event_order == ARM_REPEAT_FAIL ? 3 :
                   scenario->event_order == ARM_FIRST_FAIL ? 2 :
                   scenario->ec_status == 1 ? 1 : 2;
      guint configs = scenario->event_order == ARM_CONFIG_FAIL ||
                      scenario->event_order == ARM_CONFIG_DATA_FAIL ? 2 :
                      scenario->ec_status == 1 || scenario->event_order == ARM_FIRST_FAIL ? 0 : 1;
      if (io.sends[scenario->standalone_arm] != arms || io.sends[0x90] != configs ||
          io.events != (malformed || cancelled ? 0 : scenario->event_order == ARM_REFRESH ? 2 : 1))
        g_test_fail ();
      if (scenario->event_order == ARM_REFRESH)
        for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
          if (io.last_down_payload[2 * i] != 51 + i || io.last_down_payload[2 * i + 1] != 51 + i)
            g_test_fail ();
    }
  if (io.sends[0x34] != scenario->expected_up_sends ||
      io.sends[0x60] != scenario->expected_sleep_sends ||
      (scenario->standalone_arm == 0x32 && scenario->event_order < ARM_STATUS && io.sends[0x32] != 2) ||
      (delivered && (io.events != 1 || io.dispatches != 1)) ||
      (duplicate && io.duplicates != 1) ||
      (scenario->event_order == MULTICELL_DATA && (io.data_chunks != 3 || io.sends[0x20] != 1)) ||
      (scenario->event_order == EVENT_BEFORE_ARM_ACK && io.sends[0x32] != 1) ||
      (scenario->event_order == EVENT_BEFORE_SLEEP_ACK && io.events != 1) ||
      ((scenario->event_order == LATE_ACK_DEADLINE ||
        scenario->event_order == LATE_EVEN_DEADLINE) &&
       (io.deadline_violation || io.expected_acks || io.duplicates != 1 ||
        test_clock_us - io.ec_ack_started != ack_budget (0xae) * 1000LL || io.sends[0xae] != 1)) ||
      (scenario->success && (io.error || (self->needs_reinit && !exhausted_arm))) ||
      (exhausted_arm && (io.error || !self->needs_reinit)) ||
      (cancelled && (!io.action_cancelled ||
                    (self->needs_reinit != (io.cancelled_writes != 0)) ||
                    !g_error_matches (io.error, G_IO_ERROR, G_IO_ERROR_CANCELLED))) ||
      (excluded_send && !g_error_matches (io.error, G_USB_DEVICE_ERROR,
                           scenario->event_order == SEND_DISCONNECT ||
                           scenario->event_order == EARLIER_CLEANUP_ERROR ?
                           G_USB_DEVICE_ERROR_NO_DEVICE : G_USB_DEVICE_ERROR_CANCELLED)) ||
      (!scenario->success && !exhausted_arm && !cancelled && !excluded_send && !malformed &&
       (!g_error_matches (io.error, G_USB_DEVICE_ERROR,
                           G_USB_DEVICE_ERROR_TIMED_OUT) || !self->needs_reinit)))
    {
      g_test_message ("expected up=%u sleep=%u success=%d; needs_reinit=%d",
                      scenario->expected_up_sends, scenario->expected_sleep_sends,
                      scenario->success, self->needs_reinit);
      g_test_fail ();
    }
  g_clear_error (&io.error);
  g_clear_pointer (&io.first_up, g_bytes_unref);
  g_clear_pointer (&io.first_sleep, g_bytes_unref);
  g_clear_pointer (&io.first_down, g_bytes_unref);
  g_clear_pointer (&io.first_response, g_bytes_unref);
  g_clear_object (&action_cancel_token);
  g_clear_pointer (&self->captured_raw_image, g_free);
  g_clear_pointer (&self->rx.buf, g_free);
  goodix_milan_generation_invalidate (&self->milan_generation);
  g_clear_object (&self->cancel);
}

static guint idle_releases;
static guint idle_closes;
static guint idle_resets;
static gboolean idle_reinit_testing;
static gboolean idle_open_testing;
static gboolean idle_claim_failure;
static guint idle_usb_closes;
static guint idle_open_ssms_freed;
static FpDevice *idle_device;

static gboolean
idle_test_release (void)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (idle_device);
  g_assert_null (io.pending);
  g_assert_null (self->idle_rx_ssm);
  g_assert_false (self->rx_active);
  if ((idle_reinit_testing || idle_open_testing) && idle_releases == 0)
    {
      g_assert_cmpuint (self->rx.len, ==, 0);
      g_assert_cmpuint (self->rx.expected, ==, 0);
      g_assert_false (self->rx_idle_partial);
    }
  idle_releases++;
  return TRUE;
}

static gboolean
idle_test_claim (GError **error)
{
  if (!idle_claim_failure)
    return TRUE;
  g_set_error_literal (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_NO_DEVICE,
                       "Early claim failure");
  return FALSE;
}

static gboolean
idle_test_usb_close (void)
{
  g_assert_true (idle_open_testing);
  g_assert_null (io.pending);
  g_assert_null (FPI_DEVICE_GOODIX53X5 (idle_device)->idle_rx_ssm);
  g_assert_cmpuint (idle_releases, ==, idle_claim_failure ? 0 : 1);
  if (idle_claim_failure)
    g_assert_null (FPI_DEVICE_GOODIX53X5 (idle_device)->rx.buf);
  idle_usb_closes++;
  return TRUE;
}

static void
idle_test_open_complete (FpDevice *dev, GError *error)
{
  g_assert_true (idle_open_testing);
  g_assert_true (dev == idle_device);
  g_assert_null (io.pending);
  g_assert_null (FPI_DEVICE_GOODIX53X5 (dev)->idle_rx_ssm);
  g_assert_null (io.error);
  io.error = error;
  io.completions++;
}

static void
idle_test_close_complete (FpDevice *dev, GError *error)
{
  g_assert_true (dev == idle_device);
  g_assert_no_error (error);
  g_assert_cmpuint (idle_releases, ==, 1 + idle_resets);
  idle_closes++;
}

static gboolean
idle_test_reset (void)
{
  FpiDeviceGoodix53x5 *self = FPI_DEVICE_GOODIX53X5 (idle_device);

  g_assert_true (idle_reinit_testing || idle_open_testing);
  g_assert_cmpuint (idle_releases, ==, 1);
  g_assert_null (io.pending);
  g_assert_null (self->idle_rx_ssm);
  g_assert_false (self->rx_active);
  g_assert_false (self->rx_idle_partial);
  g_assert_cmpuint (self->rx.len, ==, 0);
  idle_resets++;
  return TRUE;
}

static FpiSsm *
idle_test_reinit_ssm (FpDevice *dev, FpiSsmHandlerCallback handler,
                      int states, int cleanup, const char *name)
{
  g_assert_true (idle_reinit_testing || idle_open_testing);
  g_assert_true (handler == goodix_open_ssm_handler);
  /* Run the actual session reset, claim and PING states, stopping before
   * firmware/TLS/calibration. No replacement reset or PING implementation. */
  return fpi_ssm_new_full (dev, handler, GOODIX_OPEN_READ_FW_VERSION,
                           GOODIX_OPEN_READ_FW_VERSION, name);
}

static void
idle_test_reinit (FpiSsm *ssm, FpDevice *dev)
{
  g_assert_true (goodix_maybe_start_reinit_subsm (ssm, dev));
}

static void
idle_test_complete (GError *error)
{
  FpiUsbTransfer *transfer = g_steal_pointer (&io.pending);
  FpiUsbTransferCallback callback = io.callback;
  gpointer data = io.user_data;
  g_autoptr(GCancellable) cancel = g_steal_pointer (&io.cancel);

  g_assert_nonnull (transfer);
  if (!error && transfer->endpoint == GOODIX_EP_OUT)
    transfer->actual_length = transfer->length;
  callback (transfer, transfer->device, data, error);
  fpi_usb_transfer_unref (transfer);
}

static void
idle_test_command_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  if (g_cancellable_is_cancelled (action_cancel_token))
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
      g_assert_true (FPI_DEVICE_GOODIX53X5 (dev)->needs_reinit);
      g_clear_error (&error);
    }
  else
    g_assert_no_error (error);
  g_assert_null (FPI_DEVICE_GOODIX53X5 (dev)->cmd_ssm);
  io.completions++;
}

static void
idle_test_ec (FpiSsm *ssm, FpDevice *dev)
{
  goodix_cmd_ec_control (ssm, dev, FALSE);
}

static void
idle_test_ping (FpiSsm *ssm, FpDevice *dev)
{
  goodix_cmd_ping (ssm, dev);
}

static void
test_idle_lifetime (gconstpointer user_data)
{
  guint which = GPOINTER_TO_UINT (user_data);
  static const Scenario scenario = { .event_order = EVENT_CANCELLED };
  FpDeviceClass *klass = g_type_class_ref (FPI_TYPE_DEVICE_GOODIX53X5);
  FpDevice *dev;
  FpDevice *weak;
  FpiDeviceGoodix53x5 *self;
  guint8 ec_data[] = { 1, 0 };
  guint8 partial_payload[96] = { 0 };
  gsize partial_length;
  g_autofree guint8 *partial = goodix_proto_build_message (
    9, 0, partial_payload, sizeof (partial_payload), TRUE, &partial_length);
  guint8 fragment[64];
  guint8 continuation[37] = { 0x91 };
  gboolean handoff = which == 2 || which == 3 || which == 4 || which == 6 || which == 11;

  memset (&io, 0, sizeof (io));
  io.scenario = &scenario;
  idle_releases = idle_closes = 0;
  idle_resets = 0;
  idle_reinit_testing = which == 11;
  action_cancel_token = g_cancellable_new ();
  klass->type = FP_DEVICE_TYPE_VIRTUAL;
  dev = g_object_new (FPI_TYPE_DEVICE_GOODIX53X5, NULL);
  g_type_class_unref (klass);
  idle_device = dev;
  weak = dev;
  g_object_add_weak_pointer (G_OBJECT (dev), (gpointer *) &weak);
  self = FPI_DEVICE_GOODIX53X5 (dev);
  self->cancel = g_cancellable_new ();
  fpi_ssm_start (fpi_ssm_new (dev, idle_test_ec, 1), idle_test_command_done);
  idle_test_complete (NULL); /* EC write */
  ack_reply (io.pending, 0xae);
  idle_test_complete (NULL);

  /* EC and its parent SSM are already gone, without any optional reply. */
  g_assert_cmpuint (io.completions, ==, 1);
  g_assert_nonnull (self->idle_rx_ssm);
  g_assert_true (io.pending->ssm == self->idle_rx_ssm);
  g_assert_true (self->rx_owner == self->idle_rx_ssm);
  g_assert_cmpuint (io.timeout, ==, 0);
  g_assert_true (io.cancel != action_cancel_token && io.cancel != self->cancel);

  if (which == 1)
    {
      reply (io.pending, 0xa, 7, ec_data, sizeof (ec_data));
      idle_test_complete (NULL);
      g_assert_nonnull (io.pending);
      g_assert_cmpuint (self->rx.len, ==, 0);
      g_assert_cmpuint (self->command_response_ready, ==, 0);
    }
  if (which == 4 || which == 11)
    {
      g_assert_cmpuint (partial_length, ==, 100);
      memcpy (fragment, partial, sizeof (fragment));
      memcpy (continuation + 1, partial + sizeof (fragment), sizeof (continuation) - 1);
      memcpy (io.pending->buffer, fragment, sizeof (fragment));
      io.pending->actual_length = sizeof (fragment);
      idle_test_complete (NULL);
      g_assert_cmpuint (self->rx.len, ==, sizeof (fragment));
      g_assert_cmpuint (io.timeout, ==, 0);
    }
  if (which == 9)
    {
      reverse_event (io.pending, 0);
      idle_test_complete (NULL);
      reverse_event (io.pending, 1);
      idle_test_complete (NULL);
      for (guint i = 0; i < GOODIX_PROFILE9_FDT_AREA_COUNT; i++)
        {
          g_assert_cmpuint (self->fdt_prior_down[i], ==, 50 + i);
          g_assert_cmpuint (self->profile9_fdt.base_manual[2 * i], ==, 50 + i);
          g_assert_cmpuint (self->profile9_fdt.base_down[2 * i], ==, 51 + i);
        }
      g_assert_cmpuint (self->pending_fdt_packet_len, ==, 0);
      g_assert_null (self->profile9_fdt.owner);
    }
  if (which == 10)
    {
      guint8 manual[28] = { 0x80 };
      reply (io.pending, 3, 3, manual, sizeof (manual));
      idle_test_complete (NULL);
      reply (io.pending, 9, 0, ec_data, 1);
      idle_test_complete (NULL);
      g_assert_cmpuint (self->command_response_ready, ==, 3);
      g_assert_cmpmem (self->manual_response, sizeof (manual), manual, sizeof (manual));
      g_assert_cmpuint (self->profile9_fdt.base_manual[0], ==, 0);
    }
  if (which == 5 || which == 8)
    {
      if (which == 5)
        idle_test_complete (g_error_new_literal (G_USB_DEVICE_ERROR,
                                                G_USB_DEVICE_ERROR_NO_DEVICE, "Removed idle reader"));
      else
        {
          reply (io.pending, 0xa, 7, ec_data, sizeof (ec_data));
          io.pending->buffer[5] ^= 1;
          g_test_expect_message ("libfprint-goodix53x5", G_LOG_LEVEL_WARNING,
                                 "*checksum validation failed*");
          idle_test_complete (NULL);
          g_test_assert_expected_messages ();
        }
      g_assert_null (io.pending);
      g_assert_null (self->idle_rx_ssm);
      g_assert_true (self->needs_reinit);
    }
  if (handoff)
    {
      guint writes = io.started_writes;
      if (which == 11)
        {
          self->needs_reinit = TRUE;
          self->usb_interface_claimed = TRUE;
          fpi_ssm_start (fpi_ssm_new (dev, idle_test_reinit, 1), idle_test_command_done);
          g_assert_cmpuint (idle_releases, ==, 0);
          g_assert_cmpuint (idle_resets, ==, 0);
          g_assert_cmpuint (self->rx.len, ==, sizeof (fragment));
        }
      else
        fpi_ssm_start (fpi_ssm_new (dev, idle_test_ping, 1), idle_test_command_done);
      g_assert_true (g_cancellable_is_cancelled (io.cancel));
      g_assert_cmpuint (io.started_writes, ==, writes);
      if (which == 6)
        g_cancellable_cancel (action_cancel_token);
      if (which == 3)
        {
          reply (io.pending, 0xa, 7, ec_data, sizeof (ec_data));
          idle_test_complete (NULL); /* Completion wins idle cancellation. */
        }
      else
        idle_test_complete (g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Joined idle"));
      g_assert_null (self->idle_rx_ssm);
      g_assert_cmpuint (io.pending->endpoint, ==, GOODIX_EP_OUT);
      if (which == 11)
        {
          g_assert_cmpuint (idle_resets, ==, 1);
          g_assert_cmpuint (self->rx.len, ==, 0);
          g_assert_false (self->rx_idle_partial);
        }
      if (which == 6)
        {
          /* Cancelled action must not start physical OUT or a fresh idle read. */
          g_assert_true (g_cancellable_is_cancelled (io.cancel));
          g_assert_cmpuint (io.started_writes, ==, writes);
        }
      idle_test_complete (which == 6 ? g_error_new_literal (
        G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled command write") : NULL);
      if (which == 4)
        {
          g_assert_true (self->rx_idle_partial);
          g_assert_cmpuint (self->rx.len, ==, sizeof (fragment));
          memcpy (io.pending->buffer, continuation, sizeof (continuation));
          io.pending->actual_length = sizeof (continuation);
          idle_test_complete (NULL);
          g_assert_false (self->rx_idle_partial);
          g_assert_cmpuint (self->command_response_ready, ==, 2);
          g_assert_cmpuint (io.completions, ==, 1);
        }
      if (which != 6)
        {
          ack_reply (io.pending, 0);
          idle_test_complete (NULL);
        }
      g_assert_cmpuint (io.completions, ==, 2);
    }

  /* Retain only the explicit idle device reference until the close joins. */
  gboolean pending = self->idle_rx_ssm != NULL;
  goodix_close (dev);
  if (pending)
    {
      g_assert_cmpuint (idle_releases, ==, 0);
      g_assert_cmpuint (idle_closes, ==, 0);
      g_assert_nonnull (self->rx.buf);
      g_object_unref (dev);
      g_assert_nonnull (weak);
      if (which == 7)
        {
          reply (io.pending, 0xa, 7, ec_data, sizeof (ec_data));
          idle_test_complete (NULL);
        }
      else
        idle_test_complete (g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Close idle join"));
    }
  else
    g_object_unref (dev);
  g_assert_cmpuint (idle_closes, ==, 1);
  g_assert_null (weak);
  g_assert_null (io.pending);
  g_clear_object (&action_cancel_token);
  idle_device = NULL;
}

static void
idle_open_cleanup_entry (FpiSsm *ssm, FpDevice *dev)
{
  /* Admit the completed reference-capture boundary; all cleanup states and
   * the failed-open completion/recovery owner below are production code. */
  if (fpi_ssm_get_cur_state (ssm) == 0)
    fpi_ssm_jump_to_state (ssm, GOODIX_OPEN_SLEEP);
  else
    goodix_open_ssm_handler (ssm, dev);
}

static void
idle_open_data_free (gpointer data)
{
  g_assert_true (data == &idle_open_ssms_freed);
  idle_open_ssms_freed++;
}

static void
test_idle_failed_open (gconstpointer user_data)
{
  guint which = GPOINTER_TO_UINT (user_data);
  gboolean recover = which < 3 || which == 6;
  guint completion = which == 8 ? 0 : which >= 6 ? 3 : which % 3;
  /* cancelled, partial completion, no idle, complete optional response */
  static const Scenario scenario = { .event_order = EVENT_CANCELLED };
  FpDeviceClass *klass = g_type_class_ref (FPI_TYPE_DEVICE_GOODIX53X5);
  g_autoptr(FpDevice) dev = NULL;
  FpiDeviceGoodix53x5 *self;
  FpiSsm *ssm;
  g_autoptr(GError) original = NULL;
  FpDevice *weak;

  memset (&io, 0, sizeof (io));
  io.scenario = &scenario;
  idle_releases = idle_closes = idle_resets = idle_usb_closes = idle_open_ssms_freed = 0;
  idle_open_testing = TRUE;
  idle_reinit_testing = FALSE;
  action_cancel_token = g_cancellable_new ();
  klass->type = FP_DEVICE_TYPE_VIRTUAL;
  dev = g_object_new (FPI_TYPE_DEVICE_GOODIX53X5, NULL);
  g_type_class_unref (klass);
  weak = dev;
  g_object_add_weak_pointer (G_OBJECT (dev), (gpointer *) &weak);
  idle_device = dev;
  self = FPI_DEVICE_GOODIX53X5 (dev);
  self->cancel = g_cancellable_new ();
  self->usb_interface_claimed = TRUE;
  self->open_ref_powered = TRUE;
  self->open_recovery_attempted = !(recover || which == 8);
  ssm = fpi_ssm_new_full (dev, idle_open_cleanup_entry, GOODIX_OPEN_NUM_STATES,
                          GOODIX_OPEN_SLEEP, "failed-open-cleanup");
  self->task_ssm = ssm;
  fpi_ssm_set_data (ssm, &idle_open_ssms_freed, idle_open_data_free);
  fpi_ssm_start (ssm, goodix_open_ssm_done);
  for (guint i = 0; i < 2; i++)
    {
      g_assert_cmpuint (io.command, ==, 0x60);
      idle_test_complete (NULL);
      idle_test_complete (g_error_new_literal (G_USB_DEVICE_ERROR,
                                               G_USB_DEVICE_ERROR_TIMED_OUT,
                                               "original sleep timeout"));
    }
  original = g_error_copy (fpi_ssm_get_error (ssm));
  g_assert_error (original, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT);
  g_assert_nonnull (strstr (original->message, "original sleep timeout"));
  g_assert_cmpuint (io.sends[0x60], ==, 2);
  g_assert_cmpuint (io.command, ==, 0xae);
  idle_test_complete (NULL);
  if (!recover)
    g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                           "*Device open failed:*original sleep timeout*");
  if (completion == 2)
    idle_test_complete (g_error_new_literal (G_USB_DEVICE_ERROR,
                                            G_USB_DEVICE_ERROR_TIMED_OUT,
                                            "later EC timeout"));
  else
    {
      ack_reply (io.pending, 0xae);
      idle_test_complete (NULL);
      g_assert_cmpuint (idle_open_ssms_freed, ==, 1);
      g_assert_null (self->task_ssm);
      g_assert_cmpuint (idle_releases, ==, 0);
      g_assert_cmpuint (idle_resets, ==, 0);
      g_assert_cmpuint (idle_usb_closes, ==, 0);
      g_assert_cmpuint (io.completions, ==, 0);
      g_assert_true (g_cancellable_is_cancelled (io.cancel));
      GoodixIdleRecv *idle = fpi_ssm_get_data (self->idle_rx_ssm);
      GError *retained = idle->joined_data;
      g_assert_error (retained, original->domain, original->code);
      g_assert_cmpstr (retained->message, ==, original->message);
      if (which == 8)
        g_cancellable_cancel (action_cancel_token);
      if (completion == 1)
        {
          guint8 payload[96] = { 0 };
          gsize len;
          g_autofree guint8 *packet = goodix_proto_build_message (
            9, 0, payload, sizeof (payload), TRUE, &len);
          g_assert_cmpuint (len, >, 64);
          memcpy (io.pending->buffer, packet, 64);
          io.pending->actual_length = 64;
          idle_test_complete (NULL); /* Partial data wins cancellation. */
        }
      else if (completion == 3)
        {
          guint8 payload[] = { 1, 0 };
          reply (io.pending, 0xa, 7, payload, sizeof (payload));
          idle_test_complete (NULL); /* Complete optional reply wins cancellation. */
        }
      else
        idle_test_complete (g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                "idle cancellation joined"));
    }
  g_assert_cmpuint (idle_releases, ==, 1);
  g_assert_false (self->rx_idle_partial);
  g_assert_cmpuint (self->rx.len, ==, 0);
  if (recover)
    {
      g_assert_cmpuint (idle_resets, ==, 1);
      g_assert_cmpuint (idle_usb_closes, ==, 0);
      g_assert_cmpuint (io.completions, ==, 0);
      g_assert_cmpuint (io.command, ==, 0);
      idle_test_complete (NULL);
      g_assert_cmpuint (io.timeout, ==, 2000);
      ack_reply (io.pending, 0);
      idle_test_complete (NULL);
      g_assert_no_error (io.error);
    }
  else
    {
      g_test_assert_expected_messages ();
      g_assert_cmpuint (idle_resets, ==, 0);
      g_assert_cmpuint (idle_usb_closes, ==, 1);
      g_assert_error (io.error, original->domain, original->code);
      g_assert_cmpstr (io.error->message, ==, original->message);
    }
  g_assert_cmpuint (io.completions, ==, 1);
  g_assert_cmpuint (idle_open_ssms_freed, ==, 1);
  g_assert_null (io.pending);
  g_assert_null (self->task_ssm);
  g_clear_error (&io.error);
  g_clear_pointer (&io.first_sleep, g_bytes_unref);
  if (recover)
    {
      goodix_close (dev);
      g_assert_cmpuint (idle_closes, ==, 1);
    }
  else
    {
      /* A failed open has already closed USB; release fixture allocations. */
      g_clear_pointer (&self->rx.buf, g_free);
      g_clear_object (&self->cancel);
      g_assert_cmpuint (idle_closes, ==, 0);
    }
  g_clear_object (&action_cancel_token);
  idle_device = NULL;
  g_clear_object (&dev);
  g_assert_null (weak);
  if (which == 5)
    {
      /* Also fail before the first receive: teardown must not allocate RX. */
      guint writes = io.started_writes;

      dev = g_object_new (FPI_TYPE_DEVICE_GOODIX53X5, NULL);
      idle_device = dev;
      self = FPI_DEVICE_GOODIX53X5 (dev);
      g_assert_null (self->rx.buf);
      self->open_recovery_attempted = TRUE;
      idle_releases = 0;
      idle_claim_failure = TRUE;
      ssm = fpi_ssm_new_full (dev, goodix_open_ssm_handler, GOODIX_OPEN_NUM_STATES,
                              GOODIX_OPEN_SLEEP, "early-failed-open");
      self->task_ssm = ssm;
      g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                             "*Device open failed: Early claim failure*");
      fpi_ssm_start (ssm, goodix_open_ssm_done);
      g_test_assert_expected_messages ();
      g_assert_error (io.error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_NO_DEVICE);
      g_assert_cmpstr (io.error->message, ==, "Early claim failure");
      g_assert_cmpuint (io.completions, ==, 2);
      g_assert_cmpuint (io.started_writes, ==, writes);
      g_assert_null (io.pending);
      g_assert_null (self->rx.buf);
      g_clear_error (&io.error);
      idle_device = NULL;
      idle_claim_failure = FALSE;
    }
  idle_open_testing = FALSE;
}

int
main (int argc, char **argv)
{
  static const Scenario control = { 0, 0, 1, 1, TRUE, EVENT_CANCELLED };
  static const Scenario drain = { 0, 0, 1, 1, TRUE, EVENT_BEFORE_SLEEP_ACK };
  static const Scenario event_race = { 0, 0, 1, 1, TRUE, EVENT_COMPLETES_DURING_CANCEL };
  /* Approved usbinterface.dll ChangeMode (RVA 0x19ec8): a zero send/ACK
   * result retries the identical sleep/FDT-arm command once. These contracts
   * intentionally fail on the pre-retry production baseline. */
  static const Scenario up_retry = { 0x34, 1, 2, 1, TRUE, EVENT_CANCELLED };
  static const Scenario sleep_retry = { 0x60, 1, 1, 2, TRUE, EVENT_CANCELLED };
  static const Scenario terminal = { 0x60, 2, 1, 2, FALSE, EVENT_CANCELLED };
  static const Scenario ec_terminal = { 0xae, 1, 1, 1, FALSE, EVENT_CANCELLED };
  static const Scenario before_arm = { 0, 0, 1, 1, TRUE, EVENT_BEFORE_ARM_ACK };
  static const Scenario sleep_duplicate_ack = { 0x60, 1, 1, 2, TRUE, DUPLICATE_SLEEP_BEFORE_EC_ACK };
  /* The late sleep ACK now crosses EC completion and reaches a genuine manual
   * response wait, instead of the nonexistent EC response wait. */
  static const Scenario sleep_duplicate_data = { 0x60, 1, 0, 2, TRUE, DUPLICATE_SLEEP_AFTER_EC_ACK, 0x36 };
  static const Scenario up_duplicate = { 0x34, 1, 2, 0, TRUE, DUPLICATE_ARM_IN_EVENT_WAIT, 0x34 };
  static const Scenario down_duplicate = { 0x32, 1, 0, 0, TRUE, DUPLICATE_ARM_IN_EVENT_WAIT, 0x32 };
  static const Scenario deadline = { 0x60, 1, 1, 2, FALSE, LATE_ACK_DEADLINE };
  static const Scenario cancel_retry = { 0x34, 1, 2, 1, FALSE, CANCEL_DURING_RETRY };
  static const Scenario early_rearm = { 0, 0, 0, 1, TRUE, EARLY_REARM_REVERSE };
  static const Scenario two_reverse = { 0x32, 1, 0, 1, TRUE, TWO_EARLY_REVERSE };
  static const Scenario send_io = { 0, 0, 2, 1, TRUE, SEND_IO_RETRY };
  static const Scenario send_timeout = { 0, 0, 2, 1, TRUE, SEND_TIMEOUT_RETRY };
  static const Scenario disconnect = { 0, 0, 1, 1, FALSE, SEND_DISCONNECT };
  static const Scenario send_cancel = { 0, 0, 1, 1, FALSE, SEND_CANCELLED };
  static const Scenario precedence = { 0x34, 1, 3, 0, TRUE, SAME_COMMAND_PRECEDENCE, 0x34 };
  static const Scenario multicell = { 0, 0, 0, 0, TRUE, MULTICELL_DATA, 0x20 };
  static const Scenario zero_ack = { 0x34, 0, 1, 1, TRUE, ACK_ZERO_THEN_ONE };
  static const Scenario two_ack = { 0x34, 0, 1, 1, TRUE, ACK_TWO_THEN_ONE };
  static const Scenario even_retry = { 0x60, 1, 1, 2, TRUE, ACK_EVEN_TIMEOUT };
  static const Scenario even_terminal = { 0x60, 2, 1, 2, FALSE, ACK_EVEN_TIMEOUT };
  static const Scenario even_ec = { 0xae, 1, 1, 1, FALSE, ACK_EVEN_TIMEOUT };
  static const Scenario even_malformed = { 0x34, 0, 1, 1, FALSE, ACK_EVEN_THEN_MALFORMED };
  static const Scenario late_even = { 0x60, 1, 1, 2, TRUE, LATE_EVEN_ACK };
  static const Scenario late_even_deadline = { 0x60, 1, 1, 2, FALSE, LATE_EVEN_DEADLINE };
  static const Scenario manual_budget = { 0x60, 1, 0, 2, TRUE, RESPONSE_BUDGET, 0x36 };
  static const Scenario config_budget = { 0x60, 1, 0, 2, TRUE, RESPONSE_BUDGET, 0x90 };
  static const Scenario manual_deadline = { 0x60, 1, 0, 2, FALSE, RESPONSE_DEADLINE, 0x36 };
  static const Scenario config_deadline = { 0x60, 1, 0, 2, FALSE, RESPONSE_DEADLINE, 0x90 };
  static const Scenario ec_zero_ack = { 0, 0, 0, 1, TRUE, EC_LATE_ACK, 0x36, 0 };
  static const Scenario ec_one_ack = { 0, 0, 0, 1, TRUE, EC_LATE_ACK, 0x36, 1 };
  static const Scenario ec_zero_data = { 0, 0, 0, 1, TRUE, EC_LATE_DATA, 0x36, 0 };
  static const Scenario ec_one_data = { 0, 0, 0, 1, TRUE, EC_LATE_DATA, 0x36, 1 };
  static const Scenario drain_down = { 0, 0, 0, 1, TRUE, DOWN_BEFORE_SLEEP_ACK };
  static const Scenario drain_reverse = { 0, 0, 0, 1, TRUE, REVERSE_BEFORE_SLEEP_ACK };
  static const Scenario drain_control = { 0, 0, 1, 1, TRUE, STOP_DRAIN_CONTROL };

  g_test_init (&argc, &argv, NULL);
  const char *failed_open_names[] = { "recovery-cancel", "recovery-partial-race", "recovery-no-idle",
                                     "final-cancel", "final-partial-race", "final-no-idle",
                                     "recovery-reply-race", "final-reply-race", "action-cancel-during-join" };
  for (guint i = 0; i < G_N_ELEMENTS (failed_open_names); i++)
    {
      g_autofree char *name = g_strdup_printf ("/goodix53x5/milan/transport/failed-open/%s",
                                              failed_open_names[i]);
      g_test_add_data_func (name, GUINT_TO_POINTER (i), test_idle_failed_open);
    }
  const char *idle_names[] = { "ack-only-close", "tail", "handoff", "handoff-race",
                              "partial-handoff", "removal", "action-cancel", "close-race", "bad-frame",
                              "stopped-fdt", "response-caches", "partial-reinit" };
  for (guint i = 0; i < G_N_ELEMENTS (idle_names); i++)
    {
      g_autofree char *name = g_strdup_printf ("/goodix53x5/milan/transport/idle/%s", idle_names[i]);
      g_test_add_data_func (name, GUINT_TO_POINTER (i), test_idle_lifetime);
    }
  static const Scenario cleanup_earlier = { 0x60, 2, 1, 2, FALSE, EARLIER_CLEANUP_ERROR };
  static const Scenario cleanup_protocol = { 0x60, 2, 1, 2, FALSE, CLEANUP_LATE_PROTO };
  g_test_add_data_func ("/goodix53x5/milan/transport/cleanup/earlier-error", &cleanup_earlier, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/cleanup/later-protocol", &cleanup_protocol, test_scenario);
  static const Scenario writes[] = {
    { 0, 0, 2, 1, TRUE, SEND_FAILED_RETRY },
    { 0, 0, 2, 1, TRUE, SEND_STALL_RETRY },
    { 0, 0, 2, 1, TRUE, SEND_INTERNAL_RETRY },
    { 0, 0, 1, 1, FALSE, WRITE_STALLED_CANCEL },
  };
  const char *write_names[] = { "transfer-error", "stall", "internal", "stalled-cancel" };
  for (guint i = 0; i < G_N_ELEMENTS (writes); i++)
    {
      g_autofree char *name = g_strdup_printf ("/goodix53x5/milan/transport/write/%s", write_names[i]);
      g_test_add_data_func (name, &writes[i], test_scenario);
    }
  static const Scenario cancel_arms[] = {
    { 0, 0, 1, 1, FALSE, CANCEL_ARM_FIRST, 0, 1 },
    { 0, 0, 1, 1, FALSE, CANCEL_ARM_FIRST, 0, 3 },
    { 0, 0, 2, 1, FALSE, CANCEL_ARM_REPEAT, 0, 3 },
    { 0, 0, 1, 1, FALSE, CANCEL_ARM_CONFIG, 0, 3 },
  };
  const char *cancel_arm_names[] = { "first", "first-status-three", "repeat", "config" };
  for (guint i = 0; i < G_N_ELEMENTS (cancel_arms); i++)
    {
      g_autofree char *name = g_strdup_printf ("/goodix53x5/milan/transport/cancel-arm/%s",
                                              cancel_arm_names[i]);
      g_test_add_data_func (name, &cancel_arms[i], test_scenario);
    }
  static const Scenario arms[] = {
    { 0, 0, 0, 0, TRUE, ARM_STATUS, 0x32, 1 },
    { 0, 0, 1, 0, TRUE, ARM_STATUS, 0x34, 1 },
    { 0, 0, 0, 0, TRUE, ARM_STATUS, 0x32, 3 },
    { 0, 0, 2, 0, TRUE, ARM_STATUS, 0x34, 3 },
    { 0, 0, 0, 0, TRUE, ARM_STATUS, 0x32, 7 },
    { 0, 0, 2, 0, TRUE, ARM_STATUS, 0x34, 7 },
    { 0, 0, 0, 0, TRUE, ARM_CONFIG_FAIL, 0x32, 3 },
    { 0, 0, 0, 0, TRUE, ARM_REPEAT_FAIL, 0x32, 3 },
    { 0, 0, 0, 0, TRUE, ARM_REPEAT_STATUS, 0x32, 3 },
    { 0, 0, 0, 0, TRUE, ARM_REFRESH, 0x32, 3 },
    { 0, 0, 0, 0, TRUE, ARM_MAX, 0x32, 3 },
    { 0, 0, 0, 0, TRUE, ARM_CONFIG_DATA_FAIL, 0x32, 3 },
    { 0, 0, 0, 0, FALSE, ARM_CONFIG_PROTO, 0x32, 3 },
    { 0, 0, 0, 0, FALSE, ARM_CONFIG_CANCEL, 0x32, 3 },
    { 0, 0, 0, 0, TRUE, ARM_FIRST_FAIL, 0x32, 3 },
  };
  const char *arm_names[] = {
    "down-one", "up-one", "down-three", "up-three", "down-seven", "up-seven",
    "config-fails-still-rearm", "repeat-fails-still-wait", "repeat-three-no-reload",
    "fresh-base-after-config", "four-arm-bound",
    "config-data-fails-still-rearm", "config-protocol-no-rearm", "config-cancel-no-rearm",
    "first-fails-still-wait",
  };
  for (guint i = 0; i < G_N_ELEMENTS (arms); i++)
    {
      g_autofree char *name = g_strdup_printf ("/goodix53x5/milan/transport/arm-repair/%s", arm_names[i]);
      g_test_add_data_func (name, &arms[i], test_scenario);
    }
  static const Scenario responses[] = {
    { 0, 0, 0, 1, TRUE, RESPONSE_EARLY, 0x36 },
    { 0, 0, 0, 1, TRUE, RESPONSE_EARLY, 0x90 },
    { 0, 0, 0, 1, TRUE, RESPONSE_RETRY, 0x36 },
    { 0, 0, 0, 1, TRUE, RESPONSE_RETRY, 0x90 },
    { 0, 0, 0, 1, TRUE, RESPONSE_EARLY_RESET, 0x36 },
    { 0, 0, 0, 1, TRUE, RESPONSE_EARLY_RESET, 0x90 },
    { 0, 0, 0, 1, TRUE, RESPONSE_STATUS, 0x90, 0 },
    { 0, 0, 0, 1, TRUE, RESPONSE_STATUS, 0x90, 1 },
    { 0, 0, 0, 1, TRUE, RESPONSE_STATUS, 0x90, 2 },
    { 0, 0, 0, 1, TRUE, RESPONSE_DUPLICATE, 0x36 },
    { 0, 0, 0, 1, TRUE, RESPONSE_DUPLICATE, 0x90 },
    { 0, 0, 0, 1, TRUE, RESPONSE_ASYNC, 0x36 },
    { 0, 0, 0, 1, TRUE, RESPONSE_ASYNC, 0x90 },
    { 0x36, 0, 1, 1, TRUE, RESPONSE_HANDOFF },
    { 0x90, 0, 1, 1, TRUE, RESPONSE_HANDOFF },
  };
  const char *response_names[] = {
    "manual-early", "config-early", "manual-response-retry", "config-response-retry",
    "manual-ready-reset", "config-ready-reset", "config-status-zero", "config-status-one",
    "config-status-two",
    "manual-duplicate", "config-duplicate", "manual-two-reverse", "config-two-reverse",
    "manual-reverse-up-handoff", "config-reverse-up-handoff",
  };
  for (guint i = 0; i < G_N_ELEMENTS (responses); i++)
    {
      g_autofree char *name = g_strdup_printf ("/goodix53x5/milan/transport/responses/%s", response_names[i]);
      g_test_add_data_func (name, &responses[i], test_scenario);
    }
  g_test_add_data_func ("/goodix53x5/milan/transport/stop-during-arm", &control, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/event-before-sleep-ack", &drain, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/event-completes-during-cancel", &event_race, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/up-ack-retry", &up_retry, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/sleep-ack-retry", &sleep_retry, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/terminal-sleep-timeout", &terminal, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/terminal-ec-timeout", &ec_terminal, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/event-before-arm-ack", &before_arm, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/late-sleep-ack-before-ec-ack", &sleep_duplicate_ack, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/late-sleep-ack-after-ec-ack", &sleep_duplicate_data, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/duplicate-up-ack-in-event-wait", &up_duplicate, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/duplicate-down-ack-in-event-wait", &down_duplicate, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/late-ack-deadline", &deadline, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/cancel-during-retry", &cancel_retry, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/early-rearm-before-cpu", &early_rearm, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/two-early-reverse-before-retry", &two_reverse, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/send-io-retry", &send_io, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/send-timeout-retry", &send_timeout, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/send-disconnect-no-retry", &disconnect, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/send-cancel-no-retry", &send_cancel, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/same-command-ack-precedence", &precedence, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ordinary-data-continuations", &multicell, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/zero-then-one", &zero_ack, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/two-then-one", &two_ack, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/exhaustion-retry", &even_retry, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/exhaustion-terminal", &even_terminal, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/ec-no-retry", &even_ec, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/malformed-no-retry", &even_malformed, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/late-even-ack", &late_even, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ack-polling/late-even-deadline", &late_even_deadline, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/budgets/manual-response", &manual_budget, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/budgets/config-response", &config_budget, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/budgets/manual-deadline", &manual_deadline, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/budgets/config-deadline", &config_deadline, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ec/late-zero-before-ack", &ec_zero_ack, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ec/late-one-before-ack", &ec_one_ack, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ec/late-zero-before-data", &ec_zero_data, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/ec/late-one-before-data", &ec_one_data, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/stop-state/down", &drain_down, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/stop-state/reverse", &drain_reverse, test_scenario);
  g_test_add_data_func ("/goodix53x5/milan/transport/stop-state/no-event", &drain_control, test_scenario);
  return g_test_run ();
}
