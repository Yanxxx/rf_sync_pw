// LED_LINK_PERIOD_MS tick for ble_led_link, implemented with Pigweed's thread API on
// FreeRTOS. GATT calls must run in the BLEAppUtil task, so the tick only
// asks BLEAppUtil to invoke LedLink_onTick() there.

#include <chrono>

#include "led_link.h"
#include "pw_chrono/system_clock.h"
#include "pw_thread/sleep.h"
#include "pw_thread/thread.h"
#include "pw_thread_freertos/context.h"
#include "pw_thread_freertos/options.h"

// From <ti/bleapp/ble_app_util/inc/bleapputil_api.h> (bStatus_t is uint8).
// Declared here so this file does not need the BLE stack's headers/options.
extern "C" uint8_t BLEAppUtil_invokeFunctionNoData(void (*callback)(char*));

namespace {

using namespace std::chrono_literals;

pw::thread::freertos::StaticContextWithStack<256> timer_context;

void TimerThread() {
  while (true) {
    pw::this_thread::sleep_for(std::chrono::milliseconds(LED_LINK_PERIOD_MS));
    BLEAppUtil_invokeFunctionNoData(LedLink_onTick);
  }
}

}  // namespace

extern "C" void LedLink_startTimer(void) {
  pw::Thread(pw::thread::freertos::Options()
                 .set_name("led_tick")
                 .set_priority(1)
                 .set_static_context(timer_context),
             TimerThread)
      .detach();
}
