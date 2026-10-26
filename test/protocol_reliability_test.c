#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "actions.h"
#include "humanize.h"

static uint32_t now_ms, baud_applied;
static uint8_t output[2048], last_keys[6], last_modifier, last_buttons;
static uint16_t output_len;
static int16_t last_dx, last_dy;
static unsigned releases;
volatile uint32_t test_aircr;
uint32_t millis(void) { return now_ms; }
void kmbox_inject_mouse(int16_t x, int16_t y, uint8_t b, int8_t wheel) {
    (void)wheel; last_dx = x; last_dy = y; last_buttons = b;
}
void kmbox_inject_keyboard(uint8_t modifier, const uint8_t keys[6]) {
    last_modifier = modifier; memcpy(last_keys, keys, 6);
}
void kmbox_schedule_click_release(uint8_t b, uint32_t d) { (void)b; (void)d; }
void kmbox_schedule_kb_release(uint8_t k, uint32_t d) { (void)k; (void)d; releases++; }
void kmbox_set_baud(uint32_t baud) { baud_applied = baud; }
uint32_t kmbox_current_baud(void) { return 4000000; }
uint16_t kmbox_tx_room(void) { return 511; }
uint32_t kmbox_frame_count(void) { return 0; }
uint32_t kmbox_error_count(void) { return 0; }
uint32_t kmbox_rx_byte_count(void) { return 0; }
uint32_t kmbox_tx_byte_count(void) { return 0; }
uint32_t kmbox_uart_overrun(void) { return 0; }
uint32_t kmbox_uart_framing(void) { return 0; }
uint32_t kmbox_uart_noise(void) { return 0; }
uint32_t kmbox_tx_overflow(void) { return 0; }
uint32_t kmbox_rx_drv_overrun(void) { return 0; }
static void capture(const uint8_t *buf, uint16_t len) {
    assert(output_len + len < sizeof(output));
    memcpy(output + output_len, buf, len); output_len += len;
}

#if TEST_FERRUM
#include "../src/ferrum.c"
#else
#include "../src/hurra.c"
#endif

int main(void) {
    act_init(); humanize_init(1000);
    // Holes before an existing key must not create duplicate held keys.
    assert(act_kb_down(4) == 0 && act_kb_down(5) == 0);
    act_kb_up(4); assert(act_kb_down(5) == 0); act_kb_up(5);
    for (unsigned i = 0; i < 6; i++) assert(last_keys[i] == 0);
    act_kb_down(4); act_kb_down(5); act_kb_up(4);
    act_kb_press(5, 10); act_kb_up(5);
    for (unsigned i = 0; i < 6; i++) assert(last_keys[i] == 0);
    assert(releases == 1);
    act_kb_down(0xE1); act_kb_up(0xE1); assert(last_modifier == 0);

    g_pos_x = INT32_MAX - 1; g_pos_y = INT32_MIN + 1;
    act_move(100, -100); assert(g_pos_x == INT32_MAX && g_pos_y == INT32_MIN);
    act_set_invert_x(true); act_move(INT16_MIN, 0); assert(last_dx == INT16_MAX);
    act_set_invert_x(false);
    act_button_set(1, 1); act_kb_down(4); act_motion_move_dur(100, 20, 100);
    humanize_return(100, 100); assert(humanize_pending());
    act_release_injected(); humanize_cancel();
    assert(g_buttons == 0 && last_buttons == 0 && !act_kb_isdown(4) && !humanize_pending());

#if TEST_FERRUM
    ferrum_init(); ferrum_set_tx(capture);
    int32_t value;
    assert(parse_int("-2147483648", 11, &value) && value == INT32_MIN);
    assert(!parse_int("-2147483649", 11, &value));
    now_ms = UINT32_MAX - 4;
    const char *command = "km.catch_xy(10)\n";
    while (*command) ferrum_feed_byte((uint8_t)*command++);
    ferrum_notify_axes(2, -3, 0);
    output_len = 0; ferrum_tick(); assert(output_len == 0);
    now_ms = 4; ferrum_tick(); assert(output_len == 0);
    now_ms = 5; ferrum_tick(); assert(output_len != 0 && !s_catch.active);
    now_ms = UINT32_MAX - 4; s_catch.active = true; s_catch.deadline = 5;
    output_len = 0; ferrum_feed_byte('x'); assert(s_catch.active && output_len == 0);
#else
    hurra_init(); hurra_set_tx(capture);
    s_tf.parser_timeout_ticks = 0;
    for (unsigned i = 0; i < 1000; i++) hurra_tick();
    assert(s_tf.parser_timeout_ticks == 0);
    now_ms += 4; hurra_tick(); assert(s_tf.parser_timeout_ticks == 4);
    now_ms += 20; hurra_tick(); assert(s_tf.parser_timeout_ticks == 5);
    now_ms = UINT32_MAX - 4;
    uint8_t duration[] = {10, 0};
    TF_Msg msg = {.len = 2, .data = duration, .frame_id = 3};
    l_catch_xy(&s_tf, &msg); output_len = 0;
    hurra_tick(); assert(s_catch.active && output_len == 0);
    now_ms = 4; hurra_tick(); assert(s_catch.active && output_len == 0);
    now_ms = 5; hurra_tick(); assert(!s_catch.active && output_len != 0);
    // A deadline of zero is valid, not an inactive sentinel.
    now_ms = UINT32_MAX - 19;
    uint8_t baud[] = {0, 9, 61, 0}; msg.data = baud; msg.len = 4;
    l_baud(&s_tf, &msg); assert(s_baud_scheduled && s_baud_apply_at == 0);
    hurra_tick(); assert(baud_applied == 0);
    now_ms = 0; hurra_tick(); assert(baud_applied == 4000000 && !s_baud_scheduled);
    now_ms = UINT32_MAX - 19; l_reboot(&s_tf, &msg);
    hurra_tick(); assert(test_aircr == 0);
    now_ms = 0; hurra_tick(); assert(test_aircr == 0x05FA0004);
#endif
    return 0;
}
