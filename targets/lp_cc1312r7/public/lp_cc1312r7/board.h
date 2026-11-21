// Board bring-up helpers for the LP_CC1312R7 Pigweed target.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opens CONFIG_UART2_0 for pw_sys_io (and therefore pw_log / pw_assert).
// Call after Board_initGeneral(); output before this call is dropped.
void lp_cc1312r7_SysIoInit(void);

#ifdef __cplusplus
}  // extern "C"
#endif
