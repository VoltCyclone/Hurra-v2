#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UART_RX_RING_SIZE 1024u
#define UART_RX_MAJOR_COUNT 16384u
#define UART_RX_COUNT_MASK (UART_RX_MAJOR_COUNT - 1u)
// Eight bit times/byte at the maximum supported 6 Mbaud, with FIFO margin.
// Default SysTick/PIT polling is <=10 ms; longer stalls fail closed.
#define UART_RX_MAX_POLL_US (((UART_RX_MAJOR_COUNT - 64u) * 8u) / 6u)
#define UART_RX_IDLE_US 1000u

typedef struct {
    uint32_t produced, consumed;
    uint32_t remaining;
    uint32_t sampled_us, sampled_ms, progress_us;
    uint32_t losses, ambiguous, discarded;
    bool recovering;
} uart_rx_tracker_t;

static inline void uart_rx_tracker_init(uart_rx_tracker_t *s, uint32_t now_ms)
{
    memset(s, 0, sizeof(*s));
    s->sampled_ms = now_ms;
}

// Caller supplies a coherent count/address sample, bracketed by timestamps.
static inline uint32_t uart_rx_sample(uart_rx_tracker_t *s, uint16_t count,
                                     uint16_t head, uint32_t now_us,
                                     uint32_t now_ms, bool fifo_empty)
{
    uint32_t remaining = count & UART_RX_COUNT_MASK;
    uint32_t delta = (s->remaining - remaining) & UART_RX_COUNT_MASK;
    if (((s->produced + delta) & (UART_RX_RING_SIZE - 1u)) != head) return 0;
    bool ambiguous = now_us - s->sampled_us >= UART_RX_MAX_POLL_US ||
                     now_ms - s->sampled_ms >= UART_RX_MAX_POLL_US / 1000u;
    s->remaining = remaining;
    s->sampled_us = now_us;
    s->sampled_ms = now_ms;
    s->produced += delta;
    if (delta || !fifo_empty) s->progress_us = now_us;
    uint32_t unread = s->produced - s->consumed;
    if (ambiguous || unread >= UART_RX_RING_SIZE) {
        s->losses++;
        if (ambiguous) s->ambiguous++;
        s->recovering = true;
        s->progress_us = now_us;
    }
    if (s->recovering) {
        s->discarded += unread;
        s->consumed = s->produced;
        if (!delta && fifo_empty && now_us - s->progress_us >= UART_RX_IDLE_US)
            s->recovering = false;
        return 0;
    }
    return unread;
}

// Atomic admission: never place a prefix of a framed reply into the TX ring.
static inline bool uart_tx_append(uint8_t *ring, uint16_t size, uint16_t *head,
                                  uint16_t tail, const uint8_t *data, uint16_t len)
{
    uint16_t mask = size - 1u;
    uint16_t used = (*head - tail) & mask;
    if (len > size - 1u - used || (!data && len)) return false;
    uint16_t first = size - *head;
    if (first > len) first = len;
    if (first) memcpy(ring + *head, data, first);
    if (len > first) memcpy(ring, data + first, len - first);
    *head = (*head + len) & mask;
    return true;
}
