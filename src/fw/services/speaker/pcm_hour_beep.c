/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/speaker/pcm_hour_beep.h"
#include "pbl/services/speaker/speaker_service.h"
#include "resource/resource.h"
#include "resource/resource_ids.auto.h"

#include <FreeRTOS.h>
#include <task.h>

#ifdef CONFIG_SPEAKER

// PCM format: 16-bit, 16 kHz, ~0.65 seconds
static const size_t HOUR_BEEP_PCM_SIZE = 10351;  // in samples (10351 samples / 16000 Hz ≈ 0.65 s)
static const uint32_t HOUR_BEEP_STREAM_CHUNK_MS = 20;
static const size_t HOUR_BEEP_CHUNK_SIZE = 1024;
static const uint32_t HOUR_BEEP_RETRY_DELAY_MS = 1;

static int16_t *s_hour_beep_pcm = NULL;
static size_t s_beep_offset = 0;

// Attempt to fill the stream buffer with as much PCM data as will fit.
// Returns true if all data has been queued, false if the buffer filled before
// all data was written (buffer is full, will need to retry later).
// Shamelessly ripped from: https://github.com/coredevices/example-apps/tree/main/speaker/pcm-resource-thing
static bool prv_fill_stream(void) {
  const uint8_t *data = (const uint8_t *)s_hour_beep_pcm;
  size_t total_bytes = HOUR_BEEP_PCM_SIZE * sizeof(int16_t);

  for (;;) {
    size_t remaining = total_bytes - s_beep_offset;
    if (remaining == 0) {
      return true;  // All data written
    }

    size_t to_write = (remaining > HOUR_BEEP_CHUNK_SIZE) ? HOUR_BEEP_CHUNK_SIZE : remaining;
    uint32_t written = speaker_service_stream_write(data + s_beep_offset, (uint32_t)to_write);

    if (written > 0) {
      s_beep_offset += written;
      continue;  // More space available, keep writing
    }

    // Buffer is full; return for retry
    return false;
  }
}

bool speaker_service_play_hour_beep(void) {
  // Load the resource on first use
  if (s_hour_beep_pcm == NULL) {
    size_t num_bytes = 0;
    const uint8_t *data = resource_get_readonly_bytes(SYSTEM_APP, RESOURCE_ID_HOUR_BEEP_PCM,
                                                      &num_bytes, true);
    if (data == NULL || num_bytes != HOUR_BEEP_PCM_SIZE * sizeof(int16_t)) {
      return false;
    }
    s_hour_beep_pcm = (int16_t *)data;
  }

  if (!speaker_service_stream_open(SpeakerPriorityNotification, HOUR_BEEP_STREAM_CHUNK_MS,
                                    SpeakerPcmFormat_16kHz_16bit)) {
    return false;
  }

  // This could be configurable, or maybe respect the current system volume setting
  speaker_service_set_volume(20);

  // Reset offset for this playback and prime the buffer immediately
  s_beep_offset = 0;

  // Fill the stream, yielding only when the buffer is full. This approach fills
  // as much as possible before yielding, avoiding unnecessary delays on
  // successful writes
  while (!prv_fill_stream()) {
    // Stream buffer filled; yield briefly to allow audio playback to consume
    // queued data before we continue writing. This prevents truncation/clicks.
    vTaskDelay(pdMS_TO_TICKS(HOUR_BEEP_RETRY_DELAY_MS));
  }

  speaker_service_stream_close();
  return true;
}
#endif
