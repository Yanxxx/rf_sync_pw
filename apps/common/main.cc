// Shared entry point for the TI RTOS-style apps on LP_CC1312R7
// (FreeRTOS + Pigweed). Each app provides `void* mainThread(void*)`, exactly
// like TI's examples, and defines APP_NAME (set by lp_cc1312r7_app()).
//
// Replaces TI's freertos/main_freertos.c: instead of pthread_create() the
// application thread is created with pw::Thread on Pigweed's FreeRTOS
// backend, and logging goes through pw_log to the XDS110 virtual COM port.

#define PW_LOG_MODULE_NAME "MAIN"

#include <ti/drivers/Board.h>
#include <ti/drivers/GPIO.h>

#include "FreeRTOS.h"
#include "lp_cc1312r7/board.h"
#include "pw_log/log.h"
#include "pw_thread/thread.h"
#include "pw_thread_freertos/context.h"
#include "pw_thread_freertos/options.h"
#include "task.h"

#ifndef APP_NAME
#error "APP_NAME must be defined (lp_cc1312r7_app() does this)"
#endif

extern "C" void* mainThread(void* arg0);

namespace {

// TI's examples use a 2096-byte pthread stack.
constexpr size_t kAppStackWords = 2096 / sizeof(StackType_t);
constexpr UBaseType_t kAppPriority = 1;

pw::thread::freertos::StaticContextWithStack<kAppStackWords> app_context;

void AppThread() {
  PW_LOG_INFO("%s (Pigweed + FreeRTOS %s) starting",
              APP_NAME,
              tskKERNEL_VERSION_NUMBER);
  mainThread(nullptr);
  PW_LOG_ERROR("mainThread returned unexpectedly");
}

}  // namespace

int main() {
  Board_init();
  GPIO_init();
  lp_cc1312r7_SysIoInit();
  // pw_sys_io writes are polled, so logging works before the scheduler runs.
  PW_LOG_INFO("Board initialized");

  pw::Thread(pw::thread::freertos::Options()
                 .set_name("app")
                 .set_priority(kAppPriority)
                 .set_static_context(app_context),
             AppThread)
      .detach();

  PW_LOG_INFO("Starting FreeRTOS scheduler");
  vTaskStartScheduler();
  PW_UNREACHABLE;
}
