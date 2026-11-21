/*
 * main() for TI basic_ble on LP_CC2652R7 with FreeRTOS + Pigweed.
 *
 * Based on TI's FreeRTOS BLE startup (examples/.../LP_EM_CC1354P10_6/
 * ble5stack/basic_ble/common/Startup/main_freertos.c, SDK 8.33.00.16) and
 * the CC2652R7 TI-RTOS7 main.c (VIMS cache setup, assert semantics).
 * Stack asserts are reported through pw_log / pw_assert instead of Display.
 * Stack overflow is handled by Pigweed's vApplicationStackOverflowHook.
 */

#define PW_LOG_MODULE_NAME "MAIN"

#include <FreeRTOS.h>
#include <task.h>

#include <ti/drivers/Board.h>
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(driverlib/vims.h)

#include <icall.h>
#include "bcomdef.h"
#include "hal_assert.h"

#include "ble_stack_api.h"
#include "ble_user_config.h"

#include "pw_assert/check.h"
#include "pw_log/log.h"

/* BLE user defined configuration (from SysConfig's ti_ble_config). */
icall_userCfg_t user0Cfg = BLE_USER_CFG;

extern assertCback_t halAssertCback;
extern void appMain(void);
void AssertHandler(uint8 assertCause, uint8 assertSubcause);

int main(void)
{
    /* Trap asserts raised in the stack. */
    halAssertCback = AssertHandler;

    Board_init();

    /* Enable iCache prefetching and the cache (as in TI's CC2652R7 main.c). */
    VIMSConfigure(VIMS_BASE, TRUE, TRUE);
    VIMSModeSet(VIMS_BASE, VIMS_MODE_ENABLED);

    RegisterAssertCback(AssertHandler);

    /* Update the stack's user configuration. */
    user0Cfg.appServiceInfo->timerTickPeriod     = ICall_getTickPeriod();
    user0Cfg.appServiceInfo->timerMaxMillisecond = ICall_getMaxMSecs();

    /* Create the ICall/stack and application tasks. */
    appMain();

    vTaskStartScheduler();
    PW_UNREACHABLE;
}

/*
 * Same semantics as TI's AssertHandler: out-of-memory and internal errors
 * are reported and execution continues; the other causes stop the system
 * (TI spins in HAL_ASSERT_SPINLOCK, here PW_CRASH reports and halts).
 */
void AssertHandler(uint8 assertCause, uint8 assertSubcause)
{
    switch (assertCause)
    {
    case HAL_ASSERT_CAUSE_OUT_OF_MEMORY:
        PW_LOG_ERROR("STACK ASSERT: out of memory");
        break;
    case HAL_ASSERT_CAUSE_INTERNAL_ERROR:
        PW_LOG_ERROR("STACK ASSERT: internal %s error",
                     assertSubcause == HAL_ASSERT_SUBCAUSE_FW_INERNAL_ERROR ? "FW" : "");
        break;
    case HAL_ASSERT_CAUSE_ICALL_ABORT:
        PW_CRASH("STACK ASSERT: ICall abort");
    case HAL_ASSERT_CAUSE_ICALL_TIMEOUT:
        PW_CRASH("STACK ASSERT: ICall timeout");
    case HAL_ASSERT_CAUSE_WRONG_API_CALL:
        PW_CRASH("STACK ASSERT: wrong API call");
    default:
        PW_CRASH("STACK ASSERT: cause 0x%02x subcause 0x%02x", assertCause, assertSubcause);
    }
}
