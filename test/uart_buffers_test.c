#include <assert.h>
#include <limits.h>
#include <string.h>
#include "uart_buffers.h"
#include "time_utils.h"

int main(void)
{
    uint8_t ring[512] = {0}, frame[264]; memset(frame, 0x68, sizeof(frame));
    uint16_t head = 0;
    assert(uart_tx_append(ring, sizeof(ring), &head, 0, frame, sizeof(frame)));
    assert(head == sizeof(frame)); // maximum current TinyFrame reply fits empty ring
    uint16_t saved = head;
    assert(!uart_tx_append(ring, sizeof(ring), &head, 0, frame, sizeof(frame)));
    assert(head == saved); // never enqueue only the beginning of a CRC frame
    head = 500;
    assert(uart_tx_append(ring, sizeof(ring), &head, 400, frame, 30));
    assert(head == 18 && memcmp(ring + 500, frame, 12) == 0 && memcmp(ring, frame + 12, 18) == 0);

    uart_rx_tracker_t s;
    uart_rx_tracker_init(&s, 0);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1023, 1023, 1000, 1, true) == 1023);
    assert(s.losses == 0);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1024, 0, 1001, 1, true) == 0);
    assert(s.recovering && s.losses == 1 && s.discarded == 1024);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1024, 0, 2100, 2, false) == 0 && s.recovering);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1024, 0, 3100, 3, true) == 0 && !s.recovering);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1032, 8, 3200, 3, true) == 8);

    uart_rx_tracker_init(&s, 0);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 3072, 0, 1000, 1, true) == 0);
    assert(s.losses == 1 && s.discarded == 3072); // three laps, same address

    uart_rx_tracker_init(&s, 0);
    s.produced = s.consumed = UINT32_MAX - 2; s.remaining = 3;
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 3, 3, 1000, 1, true) == 6);
    assert(s.produced == 3 && s.losses == 0); // CITER reload + 32-bit total rollover

    uart_rx_tracker_init(&s, 0);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1, 0, 1000, 1, true) == 0);
    assert(s.produced == 0); // incoherent count/address must not publish a byte
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1, 1, 1000, 1, true) == 1);

    uart_rx_tracker_init(&s, 0);
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT, 0, UART_RX_MAX_POLL_US, 22, true) == 0);
    assert(s.recovering && s.ambiguous == 1); // missed whole counter periods fail closed
    uart_rx_tracker_init(&s, UINT32_MAX - 1); s.sampled_us = UINT32_MAX - 100;
    assert(uart_rx_sample(&s, UART_RX_MAJOR_COUNT - 1, 1, 20, 0, true) == 1);
    assert(s.losses == 0);
    assert(!time_reached(UINT32_MAX - 2, 2) && time_reached(2, 2));
    return 0;
}
