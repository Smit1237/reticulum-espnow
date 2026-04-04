#pragma once

// Per-core CPU load measurement using FreeRTOS idle hooks.
//
// The idle hook runs once per RTOS tick (typically 1000 Hz on ESP32)
// when the core has nothing else to do. By counting how many ticks
// are idle vs total ticks elapsed, we get CPU load percentage.
//
// The hook MUST return true so the idle task can enter WFI (wait-for-
// interrupt) between ticks — returning false would prevent sleep and
// burn 100% CPU in the hook itself.
//
// Usage:
//   CpuMonitor::begin();              // in setup()
//   CpuMonitor::sample();             // call every 1-2 seconds
//   float load = CpuMonitor::load(0); // core 0 load (0.0 - 100.0)

#include <esp_freertos_hooks.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace CpuMonitor {

static volatile uint32_t _idle_cnt[2] = {0, 0};
static uint32_t _prev_cnt[2] = {0, 0};
static uint32_t _prev_time[2] = {0, 0};
static float _load[2] = {0, 0};

// Return true = call once per tick (allows WFI sleep between ticks)
// Return false = call repeatedly (prevents sleep, burns CPU!)
static bool _idle_hook_0() { _idle_cnt[0]++; return true; }
#if portNUM_PROCESSORS > 1
static bool _idle_hook_1() { _idle_cnt[1]++; return true; }
#endif

inline void begin() {
    uint32_t now = (uint32_t)xTaskGetTickCount();
    _prev_time[0] = now;
    _prev_time[1] = now;
    esp_register_freertos_idle_hook_for_cpu(_idle_hook_0, 0);
#if portNUM_PROCESSORS > 1
    esp_register_freertos_idle_hook_for_cpu(_idle_hook_1, 1);
#endif
}

// Call periodically (every 1-2 seconds) to update load values
inline void sample() {
    uint32_t now = (uint32_t)xTaskGetTickCount();

    for (int i = 0; i < portNUM_PROCESSORS; i++) {
        uint32_t idle_delta = _idle_cnt[i] - _prev_cnt[i];
        uint32_t time_delta = now - _prev_time[i];
        _prev_cnt[i] = _idle_cnt[i];
        _prev_time[i] = now;

        if (time_delta == 0) continue;

        // idle_delta = ticks where the core was idle
        // time_delta = total ticks elapsed
        // load = fraction of ticks that were NOT idle
        float idle_pct = (float)idle_delta / (float)time_delta;
        if (idle_pct > 1.0f) idle_pct = 1.0f;  // clamp
        _load[i] = 100.0f * (1.0f - idle_pct);
        if (_load[i] < 0) _load[i] = 0;
    }
}

// Get load for core (0 or 1). Returns 0-100%.
inline float load(int core) {
    if (core < 0 || core >= portNUM_PROCESSORS) return 0;
    return _load[core];
}

inline int numCores() { return portNUM_PROCESSORS; }

} // namespace CpuMonitor
