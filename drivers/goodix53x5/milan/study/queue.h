/*
 * Goodix 53x5 driver for libfprint - transient profile-9 study queue
 * Copyright (C) 2026 goodix-fp-linux-dev contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <glib.h>
#include <stdint.h>

#define GOODIX_STUDY_QUEUE_CAPACITY 20

typedef struct _GoodixMatchInfo GoodixMatchInfo;

typedef struct
{
  GoodixMatchInfo *info;
  gint             rank;
} GoodixStudyQueueEntry;

typedef struct _GoodixStudyQueue
{
  GoodixStudyQueueEntry entries[GOODIX_STUDY_QUEUE_CAPACITY];
  uint32_t               enabled_state;
  uint32_t               transaction_counter;
} GoodixStudyQueue;

typedef enum
{
  GOODIX_STUDY_QUEUE_ENQUEUED = 0,
  GOODIX_STUDY_QUEUE_DUPLICATE,
  GOODIX_STUDY_QUEUE_DISABLED,
  GOODIX_STUDY_QUEUE_INVALID,
} GoodixStudyQueueEnqueueResult;

typedef gboolean (*GoodixStudyQueueMetricFunc) (const GoodixMatchInfo *incoming,
                                                const GoodixMatchInfo *newest,
                                                gint                  *metric,
                                                gpointer               user_data);

typedef gboolean (*GoodixStudyQueueFollowupFunc) (GoodixMatchInfo *queued,
                                                  gsize            physical_slot,
                                                  gsize            triggering_index,
                                                  gsize           *selected_index,
                                                  gpointer         user_data);

GoodixStudyQueue *goodix_study_queue_new (uint32_t enabled_state,
                                          uint32_t transaction_counter);
void              goodix_study_queue_free (GoodixStudyQueue *queue);

gboolean goodix_study_queue_validate (const GoodixStudyQueue *queue);
gsize    goodix_study_queue_occupied (const GoodixStudyQueue *queue);
gsize    goodix_study_queue_allocated (const GoodixStudyQueue *queue);

GoodixStudyQueueEnqueueResult goodix_study_queue_enqueue (
  GoodixStudyQueue           *queue,
  const GoodixMatchInfo      *incoming,
  GoodixStudyQueueMetricFunc  metric_func,
  gpointer                    user_data);

gboolean goodix_study_queue_process (
  GoodixStudyQueue             *queue,
  gsize                         primary_selected_index,
  GoodixStudyQueueFollowupFunc  followup_func,
  gpointer                      user_data,
  gboolean                     *mutated);

void goodix_study_queue_disable (GoodixStudyQueue *queue);
