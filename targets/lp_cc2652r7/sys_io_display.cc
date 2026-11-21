// pw_sys_io backend for LP_CC2652R7 BLE apps.
//
// TI's BLE examples own the XDS110 UART through the Display driver
// (DisplayUart2Ansi, used by MenuModule to draw the menu). To avoid two
// drivers fighting over the UART, pw_log output is written through the same
// Display handle into its scrolling region (below the menu):
//
//   * Normal case (task context, scheduler running, Display open):
//     Display_printf(dispHandle, DisplayUart2_SCROLLING, ...). This takes
//     the Display mutex, so log lines never interleave with menu updates.
//   * Interrupt context, interrupts masked or scheduler not running (e.g.
//     PW_CRASH from a FreeRTOS assert): Display cannot be used, so bytes are
//     written by polling UART0 directly (enabling TXE like the UART2 driver).
//     This can interleave with a menu update, which is acceptable for
//     crash output.
//   * Before the app opened Display (dispHandle == NULL): output is dropped.
//
// Reading is not supported (the UART belongs to Display).

#include <cstring>

#include "pw_interrupt/context.h"
#include "pw_sys_io/sys_io.h"

#include <ti/devices/DeviceFamily.h>
#include <ti/display/Display.h>
#include <ti/display/DisplayUart2.h>
#include <ti/drivers/dpl/HwiP.h>

#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(inc/hw_types.h)
#include DeviceFamily_constructPath(inc/hw_uart.h)
#include DeviceFamily_constructPath(driverlib/uart.h)

#include "FreeRTOS.h"
#include "task.h"

// Defined by TI's MenuModule (menu_module.c) once the app opens Display.
// Weak so that apps without MenuModule still link (output is then dropped).
extern "C" Display_Handle dispHandle __attribute__((weak));

namespace {

constexpr size_t kLineBufferSize = 160;
char line_buffer[kLineBufferSize];
size_t line_length = 0;

Display_Handle Handle() { return &dispHandle != nullptr ? dispHandle : nullptr; }

bool InterruptsMasked() {
  uint32_t primask, basepri;
  __asm__ volatile("mrs %0, primask" : "=r"(primask));
  __asm__ volatile("mrs %0, basepri" : "=r"(basepri));
  return primask != 0 || basepri != 0;
}

bool CanUseDisplay() {
  return !pw::interrupt::InInterruptContext() && !InterruptsMasked() &&
         xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

// Crash-path output: poll UART0 directly (Display opened and powered it).
void RawWrite(const char* data, size_t size) {
  uintptr_t key = HwiP_disable();
  HWREG(UART0_BASE + UART_O_CTL) |= UART_CTL_TXE;
  for (size_t i = 0; i < size; ++i) {
    UARTCharPut(UART0_BASE, static_cast<uint8_t>(data[i]));
  }
  while (UARTBusy(UART0_BASE)) {
  }
  HwiP_restore(key);
}

void EmitLine(const char* data, size_t size) {
  Display_Handle handle = Handle();
  if (handle == nullptr) {
    return;
  }
  if (CanUseDisplay()) {
    Display_printf(handle, DisplayUart2_SCROLLING, 0, "%.*s",
                   static_cast<int>(size), data);
  } else {
    RawWrite(data, size);
    RawWrite("\r\n", 2);
  }
}

void FlushLine() {
  if (line_length > 0) {
    EmitLine(line_buffer, line_length);
    line_length = 0;
  }
}

}  // namespace

namespace pw::sys_io {

Status ReadByte(std::byte*) { return Status::Unimplemented(); }
Status TryReadByte(std::byte*) { return Status::Unimplemented(); }
StatusWithSize ReadBytes(ByteSpan) { return StatusWithSize::Unimplemented(); }

Status WriteByte(std::byte b) {
  char c = static_cast<char>(b);
  if (c == '\n') {
    FlushLine();
  } else if (c != '\r') {
    if (line_length == kLineBufferSize) {
      FlushLine();
    }
    line_buffer[line_length++] = c;
  }
  return Handle() != nullptr ? OkStatus() : Status::FailedPrecondition();
}

StatusWithSize WriteBytes(ConstByteSpan src) {
  for (std::byte b : src) {
    WriteByte(b).IgnoreError();  // Status only reflects whether Display is open.
  }
  return StatusWithSize(src.size());
}

StatusWithSize WriteLine(std::string_view s) {
  FlushLine();
  EmitLine(s.data(), s.size());
  return StatusWithSize(Handle() != nullptr ? OkStatus() : Status::FailedPrecondition(),
                        s.size());
}

}  // namespace pw::sys_io
