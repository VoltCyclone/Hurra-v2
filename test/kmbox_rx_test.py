"""Run production eDMA polling/copy code against registers, without ARM barriers."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/kmbox.c').read_text()


def function(name):
    match = re.search(r'^(?:static )?(?:void|bool|uint32_t) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end].replace('__asm volatile("dmb" ::: "memory");', '')


prefix = r'''
#include <assert.h>
#include <string.h>
#include "uart_buffers.h"
#include "time_utils.h"
#define MAX_KB_RELEASES 6
#define DMA_RX_RING_SIZE UART_RX_RING_SIZE
#define LINK_LED_BIT 1
#define STATE_LED_BIT 2
#define STATUS_LED_BIT 4
#define LPUART_STAT_OR 1
#define LPUART_STAT_FE 2
#define LPUART_STAT_NF 4
#define LPUART_STAT_TC 8
#define KM_TX_CH 4
static uint8_t dma_rx_ring[1024] __attribute__((aligned(1024))), rx_batch[256];
static uart_rx_tracker_t rx_tracker;
static uint32_t rx_loss_seen;
static bool rx_sample_valid;
static uint16_t rx_tail;
static uint16_t KM_RX_CITER;
static void *KM_RX_DADDR;
static uint32_t KM_UART_WATER, KM_UART_STAT, GPIO1_DR_TOGGLE, GPIO3_DR_TOGGLE;
static uint32_t uart_overrun_count, uart_framing_count, uart_noise_count, rx_drv_overrun_count;
static uint32_t rx_bytes_total, frames_ok, link_last_rx_time, last_rx_activity_time;
static struct {uint8_t kb_release_count;
               int16_t mouse_dx, mouse_dy; int8_t mouse_wheel;
               struct {uint8_t key; uint32_t at;} kb_releases[6];} inject;
static uint32_t us, ms, produced, delivered, resets, releases, cancellations;
static uint16_t tx_head, tx_tail_pos;
static uint32_t DMA_ERQ, pending_baud_rate, applied_baud;
static void baud_change_apply(uint32_t baud) {applied_baud = baud;}
static unsigned overwrite, key_releases;
static uint8_t last_key;
static void act_kb_up(uint8_t key) {last_key = key; key_releases++;}
static uint32_t millis(void) {return ms;}
static uint32_t gpt_profile_us(void) {return us;}
static void proto_reset(void) {resets++;}
static void act_release_injected(void) {releases++;}
static void humanize_cancel(void) {cancellations++;}
static void tx_flush(void) {}
static void proto_feed(const uint8_t *data, uint16_t n) {
    assert(n <= 256 && data == rx_batch); delivered += n;
}
static void advance(unsigned n) {
    for (unsigned i = 0; i < n; i++) dma_rx_ring[produced++ & 1023] = (uint8_t)i;
    KM_RX_DADDR = dma_rx_ring + (produced & 1023);
    KM_RX_CITER = UART_RX_MAJOR_COUNT - (produced & UART_RX_COUNT_MASK);
}
static void *copy_with_dma(void *dst, const void *src, size_t n) {
    memcpy(dst, src, n);
    if (overwrite) {unsigned count = overwrite; overwrite = 0; advance(count);}
    return dst;
}
#define memcpy copy_with_dma
'''
test = r'''
static void reset(void) {
    us = ms = produced = delivered = resets = releases = cancellations = 0;
    rx_loss_seen = rx_tail = 0; rx_sample_valid = false; overwrite = 0;
    KM_UART_WATER = KM_UART_STAT = 0;
    memset(&inject, 0, sizeof(inject)); uart_rx_tracker_init(&rx_tracker, 0); advance(0);
}
int main(void) {
    reset(); advance(100); kmbox_poll_heavy(); assert(delivered == 100 && resets == 0);
    reset(); advance(3072); assert(!kmbox_rx_pending());
    assert(resets == 1 && releases == 1 && cancellations == 1 && rx_tracker.discarded == 3072);
    us = 1001; ms = 1; assert(!kmbox_rx_pending() && !rx_tracker.recovering);
    advance(8); kmbox_poll_heavy(); assert(delivered == 8);
    reset(); advance(8); overwrite = 1024; kmbox_poll_heavy();
    assert(delivered == 0 && resets == 1 && rx_tracker.recovering);
    reset(); advance(8); overwrite = 3; kmbox_poll_heavy(); assert(delivered == 8 && resets == 0);
    assert(rx_available() == 3);
    reset(); KM_RX_CITER--; // inconsistent count before destination completion
    assert(rx_available() == 0 && !rx_sample_valid && rx_tracker.produced == 0);
    reset(); ms = UINT32_MAX - 9;
    kmbox_schedule_kb_release(0xE1, 10);
    assert(inject.kb_releases[0].at == 0 && inject.kb_release_count == 1);
    release_keys_due(ms); assert(key_releases == 0);
    release_keys_due(0); assert(key_releases == 1 && last_key == 0xE1 && inject.kb_release_count == 0);
    for (unsigned i = 0; i < 6; i++) kmbox_schedule_kb_release((uint8_t)(i + 4), 10);
    kmbox_schedule_kb_release(99, 10);
    assert(last_key == 99 && key_releases == 2 && inject.kb_release_count == 6);
    pending_baud_rate = 115200; KM_UART_STAT = 0; DMA_ERQ = 0;
    apply_pending_baud(); assert(applied_baud == 0 && pending_baud_rate == 115200);
    KM_UART_STAT = LPUART_STAT_TC; DMA_ERQ = 1u << KM_TX_CH;
    apply_pending_baud(); assert(applied_baud == 0);
    DMA_ERQ = 0; apply_pending_baud();
    assert(applied_baud == 115200 && pending_baud_rate == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='hurra-v2-rx-') as temp:
    unit = Path(temp) / 'test.c'
    binary = Path(temp) / 'test'
    unit.write_text(prefix + '\n'.join(function(n) for n in
        ['rx_available', 'rx_recover', 'kmbox_rx_pending', 'kmbox_poll_heavy', 'release_keys_due', 'kmbox_schedule_kb_release', 'apply_pending_baud']) + test)
    subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined', f'-I{ROOT / "src"}',
                    str(unit), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('kmbox RX adapter: passed')
