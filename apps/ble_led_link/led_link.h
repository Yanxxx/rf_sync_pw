// Interface between the BLE app (C, led_link.c) and its Pigweed timer
// thread (C++, led_link_timer.cc).
#pragma once

// How often the central sends a message (and the GREEN LEDs toggle).
#define LED_LINK_PERIOD_MS 2000

#ifdef __cplusplus
extern "C" {
#endif

// Starts a pw::Thread that calls LedLink_onTick() in the BLEAppUtil task
// context every LED_LINK_PERIOD_MS. Call before the scheduler starts.
void LedLink_startTimer(void);

// Periodic work (central: send the next write). Runs in the BLEAppUtil task.
void LedLink_onTick(char* unused);

#ifdef __cplusplus
}  // extern "C"
#endif
