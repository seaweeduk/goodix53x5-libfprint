/*
 * Goodix 53x5 driver for libfprint — sensor families
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <glib.h>

typedef struct _FpDevice FpDevice;
typedef struct _FpiSsm FpiSsm;
typedef struct _FpiDeviceGoodix53x5 FpiDeviceGoodix53x5;
typedef struct _GoodixTransport GoodixTransport;
typedef struct _GoodixCmd GoodixCmd;

/* How the bytes of one sensor family reach the driver. */
typedef struct
{
  guint8   usb_interface;
  guint8   ep_in;
  guint8   ep_out;
  /* Returns the bytes to write for the protocol message @msg, or NULL when
   * the framing cannot carry it. */
  guint8 *(*frame) (const guint8 *msg, gsize msg_len, gsize *out_len);
  /* Returns the bytes to write for a raw secure-channel record, or NULL when
   * the framing cannot carry it; NULL where the channel rides in protocol
   * messages and nothing is written raw. */
  guint8 *(*frame_record) (GBytes *record, gsize *out_len);
  /* Hands the bytes of one completed IN transfer to the receiver through
   * goodix_rx_cell(), goodix_rx_image() and goodix_mcu_publish(). */
  void    (*receive) (FpiDeviceGoodix53x5 *self, GoodixTransport *operation,
                      const guint8 *data, gsize len);
  /* Drops any partial frame kept between transfers; NULL where none is. */
  void    (*discard) (FpiDeviceGoodix53x5 *self);
} GoodixLink;

/* What a message from the sensor means beyond being a reply. */
typedef enum
{
  GOODIX_SENSOR_EVENT_NONE,
  /* The sensor lost its configuration and keys; the scan restores the
   * configuration and the next channel handshake re-checks the PSK. */
  GOODIX_SENSOR_EVENT_CONFIG_LOST,
  /* The sensor asks for a new secure-channel session, after which finger
   * detection is armed again. */
  GOODIX_SENSOR_EVENT_RECONNECT,
} GoodixSensorEvent;

typedef enum
{
  GOODIX_FDT_EVENT_DOWN = 0,
  GOODIX_FDT_EVENT_UP,
  GOODIX_FDT_EVENT_REVERSE,
  GOODIX_FDT_EVENT_NONE,
  GOODIX_FDT_EVENT_CONFIG,
  GOODIX_FDT_EVENT_ESD,
  GOODIX_FDT_EVENT_WAKE,
  GOODIX_FDT_EVENT_DEACTIVATE,
} GoodixFdtEventType;

#define GOODIX_FDT_IRQ_DOWN 0x0002
#define GOODIX_FDT_IRQ_REVERSE 0x0080
#define GOODIX_FDT_IRQ_REVERSE_ALT 0x0082
#define GOODIX_FDT_IRQ_UP 0x0200

/* The family's rules of the shared MCU protocol. */
typedef struct
{
  /* Bytes of the wake NOP's zero payload. */
  gsize    wake_payload_len;
  /* The EC-control payload that isolates the power button while the sensor
   * scans; every family ends isolation with three zero bytes. */
  guint8   ec_on_payload[3];
  /* Sets the ACK and data budgets, in ms, and permits one timeout retry. */
  gboolean (*command_budget) (guint8 command, gboolean expect_data,
                               guint *ack_ms, guint *response_ms);
  /* Checks whether a failed write may use the command's retry. */
  gboolean (*retry_write) (const GError *error);
  /* Checks whether an arm ACK needs a local config download and repeat. */
  gboolean (*repair_arm) (guint8 status);
  /* Selects up rather than down after a wake notice. */
  gboolean (*wake_arm_up) (gboolean waiting_up);
  /* Publishes a valid ESD notice's IRQ in the family's response storage. */
  gboolean (*notice_irq) (FpiDeviceGoodix53x5 *self, const guint8 *payload, gsize len);
  /* Validates and classifies a down/up notification. */
  GoodixFdtEventType (*fdt_event) (FpiDeviceGoodix53x5 *self,
                                  const guint8 *payload, gsize len, GError **error);
  /* TRUE where an ACK that does not come in time is not an error for a
   * command that expects data: the wait moves on to the data, as the
   * vendor's _IoHubExec does. FALSE retries the command. */
  gboolean ack_timeout_waits_for_data;
  /* Returns the event a complete, valid message carries, or NONE. */
  GoodixSensorEvent (*event) (guint8 category, guint8 command,
                              const guint8 *payload, gsize len);
  /* Checks whether a reply with @category/@command answers @awaited when
   * both share a response slot. */
  gboolean (*reply_answers) (guint8 category, guint8 command,
                             const GoodixCmd *awaited);
  /* Checks whether a configuration download reply reports success. */
  gboolean (*download_succeeded) (const guint8 *payload, gsize len);
  /* Checks whether the driver may send @cmd at all. */
  gboolean (*may_send) (const GoodixCmd *cmd);
} GoodixProtocol;

typedef enum
{
  /* Decrypted by the established session. */
  GOODIX_FRAME_DECRYPTED,
  /* Rejected before the session was established; says nothing about the session. */
  GOODIX_FRAME_REJECTED,
  /* Rejected on an established session; counts towards restarting it. */
  GOODIX_FRAME_REJECTED_IN_SESSION,
  /* Decrypted, but not an image; the session that decrypted it is sound. */
  GOODIX_FRAME_INVALID,
} GoodixFrameStatus;

/* Handshake attempts before a secure-channel start gives up, by what
 * started it. */
typedef struct
{
  /* Open and resume. */
  guint open;
  /* A restart after an image read the channel rejected. */
  guint after_failed_read;
  /* A new session the sensor asked for. */
  guint reconnect;
} GoodixChannelAttempts;

/* The family's secure channel. */
typedef enum
{
  GOODIX_CHANNEL_START,
  GOODIX_CHANNEL_FAILED_READ,
  GOODIX_CHANNEL_RECONNECT,
} GoodixChannelReason;

typedef struct
{
  /* The channel's name in logs. */
  const char           *name;
  GoodixChannelAttempts attempts;
  guint                 retry_delay_ms;
  /* Returns a new SSM for one handshake attempt. */
  FpiSsm             *(*attempt_new) (FpDevice *dev, GoodixChannelReason reason);
  /* Decrypts an encrypted image payload into raw12 bytes in @out. */
  GoodixFrameStatus   (*decrypt_frame) (FpiDeviceGoodix53x5 *self,
                                        const guint8 *payload, gsize len,
                                        guint8 **out, gsize *out_len);
  /* Counts one undecodable frame; returns TRUE when the channel must be
   * restarted for it. */
  gboolean            (*image_error) (FpiDeviceGoodix53x5 *self);
  /* Checks whether a failed capture may arm while channel recovery is pending. */
  gboolean            (*rearm_after_capture) (FpiDeviceGoodix53x5 *self);
  /* TRUE where starting a new session drops the finger-detect arm, so a
   * restart still pending runs before an arm. */
  gboolean              restart_disarms;
  /* Notes an ACK reporting lost configuration, before the shared loss state
   * changes; NULL where the channel keeps no state for it. */
  void                (*config_lost) (FpiDeviceGoodix53x5 *self);
  /* Forgets the session and its keys when the device closes; NULL where
   * the channel keeps nothing beyond the shared PSK. */
  void                (*discard) (FpiDeviceGoodix53x5 *self);
} GoodixChannel;

/* One sensor family: the same GF5288 sensor and pipeline behind its own MCU
 * firmware. Shared code uses these pieces and never asks which family it
 * runs. */
typedef struct
{
  GoodixLink     link;
  GoodixProtocol protocol;
  GoodixChannel  channel;
  /* Returns a new SSM running the family's bring-up, from the first probe
   * to the established secure channel. The open SSM runs it between claiming
   * the interface and the first configuration download. */
  FpiSsm      *(*open_new) (FpDevice *dev);
} GoodixFamily;

/* 5335/5385/5395: direct framing on interface 1, GTLS, HTSEC MCU. */
extern const GoodixFamily goodix_family_htsec;
/* 533c: wrapped framing on interface 0, TLS 1.2-PSK, GM168SEC MCU. */
extern const GoodixFamily goodix_family_gm168sec;
