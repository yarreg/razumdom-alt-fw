#include "inputs.h"

#include "board.h"

#define INPUT_EVENT_QUEUE_LEN 16u

static struct ddm_input_snapshot g_inputs;
static uint16_t g_di_adc_active_threshold = 1000u;
static uint16_t g_debounce_ms = 30u;
static struct ddm_input_event g_events[INPUT_EVENT_QUEUE_LEN];
static uint8_t g_event_head;
static uint8_t g_event_tail;

static void push_event(const struct ddm_input_event *event) {
    uint8_t next = (uint8_t)((g_event_head + 1u) % INPUT_EVENT_QUEUE_LEN);
    if (next == g_event_tail) {
        g_event_tail = (uint8_t)((g_event_tail + 1u) % INPUT_EVENT_QUEUE_LEN);
    }
    g_events[g_event_head] = *event;
    g_event_head = next;
}

void inputs_init(uint16_t di_adc_active_threshold, uint16_t debounce_ms) {
    board_di_init();
    inputs_set_thresholds(di_adc_active_threshold, debounce_ms);
}

void inputs_set_thresholds(uint16_t di_adc_active_threshold, uint16_t debounce_ms) {
    g_di_adc_active_threshold = di_adc_active_threshold;
    g_debounce_ms = debounce_ms;
}

void inputs_scan(uint32_t now_ms) {
    bool raw[DDM_DI_COUNT];

    board_di_raw_scan(raw, DDM_DI_COUNT, g_di_adc_active_threshold);

    for (uint8_t di = 0; di < DDM_DI_COUNT; di++) {
        if (raw[di] != g_inputs.raw_active[di]) {
            g_inputs.raw_active[di] = raw[di];
            g_inputs.last_change_ms[di] = now_ms;
            struct ddm_input_event event = {
                    .type = DDM_INPUT_EVENT_RAW_CHANGE,
                    .di = di,
                    .raw_active = raw[di],
                    .debounced_pressed = g_inputs.debounced_pressed[di],
                    .stable_ms = 0u,
                    .duration_ms = 0u,
            };
            push_event(&event);
        }

        uint32_t stable_ms = now_ms - g_inputs.last_change_ms[di];
        if (stable_ms >= g_debounce_ms && g_inputs.debounced_pressed[di] != g_inputs.raw_active[di]) {
            bool pressed = g_inputs.raw_active[di];
            g_inputs.debounced_pressed[di] = pressed;
            g_inputs.stable_since_ms[di] = now_ms;
            if (pressed) {
                g_inputs.press_started_ms[di] = now_ms;
            }

            struct ddm_input_event event = {
                    .type = pressed ? DDM_INPUT_EVENT_DEBOUNCED_PRESS : DDM_INPUT_EVENT_DEBOUNCED_RELEASE,
                    .di = di,
                    .raw_active = g_inputs.raw_active[di],
                    .debounced_pressed = pressed,
                    .stable_ms = stable_ms,
                    .duration_ms = pressed ? 0u : now_ms - g_inputs.press_started_ms[di],
            };
            push_event(&event);
        }
    }
}

bool inputs_pop_event(struct ddm_input_event *event) {
    if (g_event_tail == g_event_head) {
        return false;
    }
    *event = g_events[g_event_tail];
    g_event_tail = (uint8_t)((g_event_tail + 1u) % INPUT_EVENT_QUEUE_LEN);
    return true;
}

const struct ddm_input_snapshot *inputs_snapshot(void) {
    return &g_inputs;
}

bool inputs_di_pressed(uint8_t di) {
    return di < DDM_DI_COUNT && g_inputs.debounced_pressed[di];
}

bool inputs_di_raw_active(uint8_t di) {
    return di < DDM_DI_COUNT && g_inputs.raw_active[di];
}
