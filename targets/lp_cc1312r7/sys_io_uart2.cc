// pw_sys_io backend for TI CC13x2x7/CC26x2x7 using UART0 on the LaunchPad's
// XDS110 virtual COM port (SysConfig instance CONFIG_UART2_0, 115200 8N1).
//
// TI's UART2 driver is used to power up and configure the peripheral and for
// blocking reads. Writes poll the UART FIFO through driverlib so that logging
// also works with interrupts disabled (e.g. from PW_CRASH / FreeRTOS asserts).
//
// Note: UART2CC26X2 keeps the transmitter disabled (UART_CTL_TXE = 0) except
// while UART2_write() is in progress. Polled writes must therefore enable TXE
// themselves and disable it again afterwards, mirroring the driver.

#include "pw_sys_io/sys_io.h"

#include <ti/devices/DeviceFamily.h>
#include <ti/drivers/UART2.h>
#include <ti/drivers/dpl/HwiP.h>

#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(inc/hw_types.h)
#include DeviceFamily_constructPath(inc/hw_uart.h)
#include DeviceFamily_constructPath(driverlib/uart.h)

#include "lp_cc1312r7/board.h"

// Every app's SysConfig must define a UART2 instance named CONFIG_UART2_0 (see
// README). Its index is provided by the app's generated ti_drivers_config.c,
// so this board code does not depend on any particular app's SysConfig.
extern "C" const uint_least8_t CONFIG_UART2_0_CONST;

namespace {

UART2_Handle uart_handle = nullptr;

void SetTxEnabled(bool enabled) {
  // CTL is also modified by UART2_read() (RXE), so read-modify-write it with
  // interrupts disabled.
  uintptr_t key = HwiP_disable();
  if (enabled) {
    HWREG(UART0_BASE + UART_O_CTL) |= UART_CTL_TXE;
  } else {
    HWREG(UART0_BASE + UART_O_CTL) &= ~UART_CTL_TXE;
  }
  HwiP_restore(key);
}

void TxBegin() { SetTxEnabled(true); }

void PutByte(std::byte b) { UARTCharPut(UART0_BASE, static_cast<uint8_t>(b)); }

void TxEnd() {
  // Wait until the last stop bit is out (so the power driver cannot enter
  // standby with data in the shift register), then hand TX back to the
  // driver in the state it expects.
  while (UARTBusy(UART0_BASE)) {
  }
  SetTxEnabled(false);
}

}  // namespace

extern "C" void lp_cc1312r7_SysIoInit(void) {
  if (uart_handle != nullptr) {
    return;
  }
  UART2_Params params;
  UART2_Params_init(&params);
  params.baudRate = 115200;
  params.readMode = UART2_Mode_BLOCKING;
  params.writeMode = UART2_Mode_BLOCKING;
  uart_handle = UART2_open(CONFIG_UART2_0_CONST, &params);
}

namespace pw::sys_io {

Status ReadByte(std::byte* dest) {
  if (uart_handle == nullptr) {
    return Status::FailedPrecondition();
  }
  size_t read = 0;
  if (UART2_read(uart_handle, dest, 1, &read) != UART2_STATUS_SUCCESS ||
      read != 1) {
    return Status::DataLoss();
  }
  return OkStatus();
}

Status TryReadByte(std::byte*) { return Status::Unimplemented(); }

Status WriteByte(std::byte b) {
  if (uart_handle == nullptr) {
    return Status::FailedPrecondition();
  }
  TxBegin();
  PutByte(b);
  TxEnd();
  return OkStatus();
}

StatusWithSize ReadBytes(ByteSpan dest) {
  for (size_t i = 0; i < dest.size(); ++i) {
    Status status = ReadByte(&dest[i]);
    if (!status.ok()) {
      return StatusWithSize(status, i);
    }
  }
  return StatusWithSize(dest.size());
}

StatusWithSize WriteBytes(ConstByteSpan src) {
  if (uart_handle == nullptr) {
    return StatusWithSize::FailedPrecondition();
  }
  TxBegin();
  for (std::byte b : src) {
    PutByte(b);
  }
  TxEnd();
  return StatusWithSize(src.size());
}

StatusWithSize WriteLine(std::string_view s) {
  StatusWithSize result = WriteBytes(as_bytes(span(s)));
  if (!result.ok()) {
    return result;
  }
  StatusWithSize newline = WriteBytes(as_bytes(span("\r\n", 2)));
  return StatusWithSize(newline.status(), result.size() + newline.size());
}

}  // namespace pw::sys_io
