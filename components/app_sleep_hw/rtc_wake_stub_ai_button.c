// SPDX-License-Identifier: Apache-2.0
/* The file name starts with rtc_wake_stub, so the linker puts all of its code
 * and constants in RTC memory. The stub uses only inline register helpers
 * and ROM calls: it runs before the bootloader, with no flash cache, no IDF
 * drivers and no log. A power cycle clears RTC memory, and the stub with
 * it, so a broken stub cannot keep the board from booting. */
#include <stddef.h>
#include <string.h>
#include "app_sleep_wake_stub.h"
#include "esp_attr.h"
#include "hal/clk_tree_ll.h"
#include "hal/mwdt_ll.h"
#include "hal/pmu_ll.h"
#include "hal/rtc_io_ll.h"
#include "hal/rtc_timer_ll.h"
#include "hal/rwdt_ll.h"
#include "riscv/rv_utils.h"
#include "soc/pmu_struct.h"
#include "soc/rtc.h"
#include "soc/timer_group_struct.h"

/* The pinned ESP-IDF (master 7b9cc1ac) does not enable its wake-stub support
 * for the ESP32-S31 yet (ESP_ROM_SUPPORT_DEEP_SLEEP_WAKEUP_STUB is set only
 * from release/v6.2 on, commit ea4eeb97, which turns on the same ROM call).
 * So the entry and the ROM registration are done here, as v6.2's
 * esp_sleep/sleep_modes.c and sleep_wake_stub.c do them. */
typedef void (*stub_entry_fn_t)(void);
extern void esp_rom_set_rtc_wake_addr(stub_entry_fn_t entry, size_t length);   /* In ROM. */
extern char _rtc_text_start[], _rtc_force_fast_end[];

/* PMU wake-cause bits on the ESP32-S31 (esp_hw_support's private pmu_bit_defs.h). */
#define CAUSE_EXT1 (1U << 12)

#define STUB_MAGIC 0x53545542U   /* "STUB". */

typedef struct {
    uint32_t magic;
    app_wake_stub_result_t result;
} stub_state_t;

/* RTC_DATA, outside the CRC'd range (.rtc.text and .rtc.force_fast), so the
 * stub can count into it without breaking the check on the next wake. */
static RTC_DATA_ATTR stub_state_t s_stub;

void pixelmate_wake_stub(void);   /* Not static: the entry calls it by name from asm. */
static void stub_entry(void);

/* The ROM checks a CRC of RTC fast memory up to here before it jumps to the
 * entry; call again whenever that range may have changed. */
static inline __attribute__((always_inline)) void stub_register(void)
{
    esp_rom_set_rtc_wake_addr(&stub_entry, (size_t)(_rtc_force_fast_end - _rtc_text_start));
}

/* True once `ms` have passed since `start` (RTC timer ticks). No division:
 * libgcc's would sit in flash, out of the stub's reach. */
static inline __attribute__((always_inline)) bool stub_elapsed(uint64_t start, uint32_t ms)
{
    /* Slow-clock period in microseconds, Q19 fixed point, kept by the IDF. */
    const uint64_t ticks = rtc_timer_ll_get_cycle_count(0) - start;
    return ((ticks * clk_ll_rtc_slow_load_cal()) >> RTC_CLK_CAL_FRACT) >= (uint64_t)ms * 1000U;
}

static inline __attribute__((always_inline)) void stub_feed(void)
{
    /* The flashboot watchdogs run until the bootloader turns them off. */
    rwdt_dev_t *rwdt = RWDT_DEV_GET();
    rwdt_ll_write_protect_disable(rwdt);
    rwdt_ll_feed(rwdt);
    rwdt_ll_write_protect_enable(rwdt);
    mwdt_ll_write_protect_disable(&TIMERG0);
    mwdt_ll_feed(&TIMERG0);
    mwdt_ll_write_protect_enable(&TIMERG0);
}

static inline __attribute__((always_inline)) bool stub_button_down(void)
{
    return rtcio_ll_get_level(APP_WAKE_STUB_GPIO) == 0;
}

/* Straight back to deep sleep with the configuration the IDF left in the PMU. */
static inline __attribute__((always_inline, noreturn)) void stub_sleep_again(void)
{
    stub_register();
    pmu_ll_hp_clear_wakeup_intr_status(&PMU);
    pmu_ll_hp_clear_reject_intr_status(&PMU);
    pmu_ll_hp_clear_reject_cause(&PMU);
    pmu_ll_hp_set_sleep_enable(&PMU);
    for (;;) {
        rv_utils_wait_for_intr();
    }
}

void __attribute__((used, noinline)) pixelmate_wake_stub(void)
{
    ++s_stub.result.runs;
    const uint32_t cause = pmu_ll_hp_get_wakeup_cause(&PMU);
    s_stub.result.last_cause = cause;
    s_stub.result.confirmed = false;
    s_stub.result.held = false;
    if (!(cause & CAUSE_EXT1)) {
        return;   /* The backstop timer: boot as usual. */
    }
    const uint64_t start = rtc_timer_ll_get_cycle_count(0);
    while (!stub_elapsed(start, APP_WAKE_STUB_DEBOUNCE_MS)) {
        stub_feed();
    }
    if (!stub_button_down()) {
        /* A bump, not a press: back to sleep. The backstop timer is still set. */
        ++s_stub.result.resleeps;
        pmu_ll_ext1_clear_wakeup_status();
        stub_sleep_again();
    }
    while (stub_button_down() && !stub_elapsed(start, APP_WAKE_STUB_HOLD_MS)) {
        stub_feed();
    }
    s_stub.result.held = stub_button_down();
    s_stub.result.confirmed = true;
    /* Hand the pad back so the bootloader's GPIO read sees its real level. */
    rtcio_ll_force_hold_disable(APP_WAKE_STUB_GPIO);
    rtcio_ll_function_select(APP_WAKE_STUB_GPIO, RTCIO_LL_FUNC_DIGITAL);
}

/* First thing in RTC fast memory, where the ROM jumps on a deep-sleep wake.
 * Kept to a call so there is nothing before the entry outside the CRC. */
static void __attribute__((section(".rtc.entry.text"), used)) stub_entry(void)
{
    __asm__ __volatile__(
        "addi sp, sp, -16 \n"
        "sw   ra, 0(sp)   \n"
        "jal  ra, pixelmate_wake_stub \n"
        "lw   ra, 0(sp)   \n"
        "addi sp, sp, 16  \n");
}

void app_wake_stub_arm(void)
{
    memset(&s_stub, 0, sizeof(s_stub));
    s_stub.magic = STUB_MAGIC;
    stub_register();
}

bool app_wake_stub_take(app_wake_stub_result_t *out)
{
    if (s_stub.magic != STUB_MAGIC) {
        return false;
    }
    if (out) {
        *out = s_stub.result;
    }
    s_stub.magic = 0;
    return true;
}
