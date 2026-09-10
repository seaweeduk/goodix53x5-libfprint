/*
 * Goodix profile-9 transport/coordinator scheduling contracts.
 * Copyright (C) 2026 goodix53x5-libfprint contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "test-goodix53x5-milan-transport-fixture.h"

TestUsb usb;
gint64 test_clock_us;
GCancellable *action_cancel_token;

gint64
test_monotonic_time (void)
{
  return test_clock_us;
}

GCancellable *
test_action_cancellable (FpDevice *dev)
{
  return action_cancel_token;
}

FpDevice *
fixture_device_new (void)
{
  FpDeviceClass *klass = g_type_class_ref (FPI_TYPE_DEVICE_GOODIX53X5);
  FpDevice *dev;

  g_assert_null (usb.pending);
  g_assert_null (usb.cancel);
  g_assert_null (action_cancel_token);
  test_clock_us = G_USEC_PER_SEC;
  action_cancel_token = g_cancellable_new ();
  klass->type = FP_DEVICE_TYPE_VIRTUAL;
  dev = g_object_new (FPI_TYPE_DEVICE_GOODIX53X5, NULL);
  g_type_class_unref (klass);
  return dev;
}

void
mock_submit (FpiUsbTransfer *transfer, guint timeout, GCancellable *cancel,
             FpiUsbTransferCallback callback, gpointer user_data)
{
  /* Neither IN nor OUT may overlap an unresolved physical completion. */
  g_assert_null (usb.pending);
  usb.pending = transfer;
  usb.callback = callback;
  usb.user_data = user_data;
  usb.cancel = cancel ? g_object_ref (cancel) : NULL;
  usb.timeout = timeout;
  check_submit (transfer, timeout, cancel);
}

void
fixture_complete (GError *error)
{
  FpiUsbTransfer *transfer = g_steal_pointer (&usb.pending);
  FpiUsbTransferCallback callback = usb.callback;
  gpointer data = usb.user_data;
  g_autoptr(GCancellable) cancel = g_steal_pointer (&usb.cancel);

  g_assert_nonnull (transfer);
  usb.callback = NULL;
  usb.user_data = NULL;
  if (!error && !(transfer->endpoint & FPI_USB_ENDPOINT_IN))
    transfer->actual_length = transfer->length;
  callback (transfer, transfer->device, data, error);
  fpi_usb_transfer_unref (transfer);
}

void
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


gsize
fixture_fragment (const guint8 *packet, gsize size, gsize offset)
{
  gsize header = offset ? 1 : 0;
  gsize count = MIN (size - offset, 64 - header);

  g_assert_nonnull (usb.pending);
  if (header)
    usb.pending->buffer[0] = packet[0] | 1;
  memcpy (usb.pending->buffer + header, packet + offset, count);
  usb.pending->actual_length = count + header;
  return offset + count;
}

void
ack_reply (FpiUsbTransfer *transfer, guint8 command)
{
  guint8 ack[] = { command, 1 };
  reply (transfer, 0x0b, 0, ack, sizeof (ack));
}

void
add_transport_case (const char *name, gconstpointer data, GTestDataFunc test)
{
  g_autofree char *path = g_strconcat ("/goodix53x5/milan/transport/", name, NULL);
  g_test_add_data_func (path, data, test);
}
