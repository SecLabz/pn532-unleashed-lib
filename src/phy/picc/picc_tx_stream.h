#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initializes the shared 8/fc load-modulation state machine. */
bool tag_tx_stream_init(void);

/* Resets the state machine and its FIFO before building one response. */
void tag_tx_stream_prepare(void);

/* Queues one protocol-sized load pattern, least-significant bit first. */
void tag_tx_stream_put(uint32_t pattern);

/* Starts a prepared stream whose first pattern is already queued. */
void tag_tx_stream_start(void);

/* Waits for the last pattern to finish, stops the SM, and releases the load. */
void tag_tx_stream_finish(void);

/* Stops the state machine immediately and releases the load. */
void tag_tx_stream_stop(void);

#ifdef PNU_RF_TRACE
/* Sticky underrun flag before the deliberate terminal FIFO stall. */
bool tag_tx_stream_underrun(void);
#endif
