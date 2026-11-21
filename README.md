# rf_sync_pw — TI CC13xx/CC26xx + Pigweed（Sub-1 GHz 和 BLE）

把 TI SimpleLink 示例改用 Bazel 和 [Pigweed](https://pigweed.dev) 构建，可在 **Ubuntu / macOS / Windows** 上开发。

| 板子 | 应用 | 说明 |
|---|---|---|
| LP_CC1312R7 | `rf_sync_tx` / `rf_sync_rx` | TI `rfSynchronizedPacketTx/Rx`：TX 每 500 ms 发一个信标，RX 与它同步接收 |
| LP_CC2652R7 | `basic_ble` | TI `basic_ble`（带串口菜单的 BLE 主从机），从 TI-RTOS7 移植到 FreeRTOS + GCC |
| LP_CC2652R7 | `ble_led_link`（`ble_led_peripheral` + `ble_led_central`） | 两块板之间的 BLE 通信测试，用 LED 显示消息收发（见第 4 节） |

## 快速开始（VS Code）

1. 安装 **Git**、**Python 3.10+**、**VS Code**，以及用来烧录的 **[TI UniFlash](https://www.ti.com/tool/UNIFLASH)**。只有这四样需要手动安装。
2. 克隆仓库，用 VS Code 打开它。
3. 按 `Ctrl/Cmd+Shift+B` 编译（任务 "Build: LP_CC1312R7 apps"）。
4. 插上板子，运行任务 **"Flash app"**（菜单 Terminal → Run Task），选择应用即可。只连了一块板子时，序列号留空。

**第一次编译会自动下载所有依赖**：bazelisk、Bazel、Arm GCC、Pigweed、FreeRTOS，以及 TI SDK（从 TI 的 GitHub 仓库按需稀疏下载，约 15 MB）。在我们的环境里，从零开始到编译完成大约 2 分钟，之后都是增量编译。

不需要安装：TI SDK、SysConfig（只在修改 `.syscfg` 时需要）、`arm-none-eabi-gcc`、bazelisk。

## 1. 安装说明

| 工具 | Ubuntu | macOS | Windows |
|---|---|---|---|
| Git | `apt install git` | Xcode 命令行工具 / Homebrew | git-scm.com |
| Python 3.10+ | 系统自带 | 系统自带 / Homebrew | python.org（安装时勾选 "Add to PATH"） |
| TI UniFlash | 安装到 `~/ti/` | 安装到 `~/ti/` 或 `/Applications/ti/` | 安装到 `C:\ti\` |

- **UniFlash 同时会安装 XDS110 的 USB 权限和驱动**：Linux 上是 udev 规则，需要 sudo；Windows 上是 TI 的仿真器驱动。如果它不在默认位置，设置环境变量 `TI_UNIFLASH` 指向 `dslite.sh` 或 `dslite.bat`。
- **为什么不用 OpenOCD**：这些 LaunchPad 上，XDS110 和芯片之间只连了 2 线 cJTAG，而 OpenOCD 的 XDS110 驱动只支持 4 线 JTAG 和 SWD，连不上芯片（已实测）。
- **Bazel 版本**由 `.bazelversion` 固定；bazelisk 的版本和 SHA-256 固定在 `tools/bazel.py` 里。
- **TI SDK** 固定为 `lpf2-8.33.00.16`，并校验 commit。如果想用本地安装的 SDK，在 `user.bazelrc` 里加一行：`common --repo_env=TI_SIMPLELINK_SDK=/path/to/sdk`。

Windows 上的额外设置：
- 打开**开发者模式**（设置 → 隐私和安全性 → 开发者选项），然后在 `user.bazelrc` 里加上 `startup --windows_enable_symlinks`。
- 仓库路径尽量短，比如 `C:\src\rf_sync_pw`，避免超过 Windows 的路径长度限制。

## 2. 命令行编译

```sh
python3 tools/bazel.py build //...                        # LP_CC1312R7 的应用（默认）
python3 tools/bazel.py build --config=lp_cc2652r7 //...   # LP_CC2652R7 的 BLE 应用
```

Windows 上把 `python3` 换成 `python`。如果已经装了 bazelisk，也可以直接用 `bazelisk` 代替 `python3 tools/bazel.py`。

两条命令各自只编自己板子的应用，另一块板子的目标会被自动跳过。产物在 `bazel-bin/apps/<目录>/` 下：`.elf` 用于调试和烧录，`.bin` 是 Flash 镜像。

构建是可复现的：在全新环境里编出来的 5 个固件，和开发机上编出来的逐字节一致（已核对）。

## 3. 烧录和查看日志

```sh
python3 tools/flash.py rf_sync_tx      # 只连了一块板子时
python3 tools/flash.py --list          # 列出所有 XDS110 的序列号
python3 tools/flash.py rf_sync_tx --serial <序列号A>   # 连了多块板子时，必须指定
python3 tools/flash.py rf_sync_rx --serial <序列号B>
python3 tools/flash.py ble_led_link:ble_led_central --serial <序列号>   # <目录>:<目标>
```

板子型号（用哪个 Bazel 平台、哪个 `.ccxml`）是从应用的 `.syscfg` 里自动读出来的，不需要手动指定。

Windows 上把 `python3` 换成 `python`。

脚本会先编译，然后用 UniFlash 的 `dslite` 通过 XDS110 烧录、校验，最后复位板子让固件开始运行（Board Reset）。连了多块板子却没有给 `--serial` 时，脚本会直接报错，不会随便挑一块烧。序列号可以用 `--list` 查，板子背面的标签上也有。

日志从 XDS110 的 **Application/User UART** 口输出，参数是 115200 8N1：

| 系统 | 串口 | 示例命令 |
|---|---|---|
| Ubuntu | `/dev/serial/by-id/*<序列号>-if00`（多块板子时用这个来区分） | `python3 -m serial.tools.miniterm /dev/ttyACM0 115200` |
| macOS | `/dev/cu.usbmodem*1` | 同上 |
| Windows | 设备管理器里的 "XDS110 Class Application/User UART" | PuTTY 或 VS Code Serial Monitor |

正常启动后会打印：

```
INF  Board initialized
INF  Starting FreeRTOS scheduler
INF  rfSynchronizedPacketTx (Pigweed + FreeRTOS V11.1.0+) starting
```

RX 板打印的是 `rfSynchronizedPacketRx ... starting`。烧录过程中板子会复位几次，所以这几行可能会重复出现，这是正常的。

验证两块板子能正常通信：按 TX 板上的按键 BTN-1，TX 的绿灯会翻转；RX 收到下一个信标后，它的绿灯也会跟着变成同样的状态。

## 4. BLE 通信测试（两块 LP_CC2652R7）

```sh
python3 tools/flash.py ble_led_link:ble_led_peripheral --serial <板子A>
python3 tools/flash.py ble_led_link:ble_led_central    --serial <板子B>
```

从机广播名字 `PW-LED-P`。主机找到它之后自动连接，发现 GATT 特征值 `0xFFF1`（TI Simple GATT Profile），然后每 2 秒写入一次，写入的值在 0 和 1 之间交替，使用带响应的写入。周期由 `apps/ble_led_link/led_link.h` 里的 `LED_LINK_PERIOD_MS` 决定。断线后主机会自动重新扫描、重新连接，所以可以一直运行。

两块板子的 LED 含义相同：

| LED | 含义 |
|---|---|
| 红灯 D106 | 常亮 = BLE 已连接 |
| 绿灯 D107 | 主机：显示最近一次被从机确认的值；从机：显示收到的值 |

**通信正常时，两块板子的红灯常亮，绿灯同步地每 2 秒翻转一次。**

串口日志会打印序号，可以逐条对照：

```
从机: INF  RX #12: value=0 -> GREEN off
主机: INF  TX #12: value=0
主机: INF  ACK #12: value=0 delivered
```

刚连接时主机可能会打印一两次 `Write failed to start (status 0x16)`。`0x16` 是 `blePending`：协议栈此时还有别的 GATT 交互没有完成。这种情况下主机不增加序号，到下一个周期再发，所以不会丢消息。

实测（2026-11-20）：两轮测试分别是 52/52 和 41/41 条消息全部送达，值和顺序完全一致，没有缺号，RSSI −31 dBm。

## 5. 修改 SysConfig 配置

只有修改 `.syscfg` 时才需要这一节，平时编译用不到。这时需要在本机安装 **TI SysConfig** 和 **TI SimpleLink SDK 8.33.00.16**：SysConfig 要读取 SDK 里的元数据，而 Bazel 自动下载的那份 SDK 只包含编译需要的目录，没有这些元数据。`tools/syscfg_gen.py` 会在 `~/ti`（Windows 上是 `C:\ti`）下找它们，也可以用环境变量 `TI_SIMPLELINK_SDK` 和 `TI_SYSCONFIG` 指定位置。

每个应用有自己的 `.syscfg`，里面是引脚、射频参数和 CCFG。

1. 编辑 `apps/<应用>/*.syscfg`，或者运行 `python3 tools/syscfg_gen.py apps/rf_sync_rx --gui` 打开 GUI。
2. 运行 `python3 tools/syscfg_gen.py apps/rf_sync_rx` 重新生成 `apps/rf_sync_rx/syscfg_gen/`，或者用 `--all` 一次生成所有应用。
3. 把生成的文件一起提交。

生成的文件是提交进仓库的，所以平时编译不需要安装 SysConfig。

FreeRTOS 的配置是**整块板子共用的**：内核只有一份，`FreeRTOSConfig.h` 也只有一份。如果某个应用的 SysConfig 改了 FreeRTOS 设置、和其它应用不一致，脚本会报错，防止它悄悄覆盖掉别的应用的配置。

## 6. 新增一个应用

1. 新建目录 `apps/<名字>/`，放入应用代码（要提供 `void *mainThread(void *arg0)`，和 TI 示例的写法一样）以及一个 `.syscfg`。
2. `.syscfg` 末尾要加上 UART2 实例，`pw_log` 需要用它。可以从 `apps/rf_sync_tx/rfSynchronizedPacketTx.syscfg` 的最后一段直接复制，实例名必须是 `CONFIG_UART2_0`。
3. 运行 `python3 tools/syscfg_gen.py apps/<名字>`。
4. 写 `apps/<名字>/BUILD.bazel`：

```python
load("//targets/lp_cc1312r7:app.bzl", "lp_cc1312r7_app")

lp_cc1312r7_app(
    name = "<名字>",
    srcs = ["<名字>.c"],
)
```

## 7. 目录结构

```
MODULE.bazel                  依赖：Pigweed（固定 commit）、FreeRTOS、Arm 工具链、TI SDK
.bazelrc                      默认平台 = LP_CC1312R7；--config=lp_cc2652r7 切换到 BLE 板
apps/                         应用层
  common/                     CC1312R7 应用共用：main.cc（pw::Thread 启动 mainThread）、StateMachine
  rf_sync_tx/  rf_sync_rx/    TI RF 示例 + 各自的 .syscfg 和 syscfg_gen/
  ble_common/startup/         BLE 应用共用：main.c（FreeRTOS 版启动）、osal_icall_ble.c
  basic_ble/                  TI basic_ble 应用代码 + .syscfg
  ble_led_link/               两板 BLE + LED 通信测试（led_link.c，定时器用 pw::Thread）
targets/                      板级 + RTOS 层
  ti_common/                  两块板共用：FreeRTOSConfig.h 包装、FreeRTOS 胶水代码
  lp_cc1312r7/                platform、app.bzl、UART 版 pw_sys_io、链接脚本、ccxml
  lp_cc2652r7/                platform、ble.bzl、Display 版 pw_sys_io、BLE 链接脚本、ccxml
third_party/
  ti_simplelink/              @ti_simplelink_sdk 仓库规则、SDK 的 BUILD 文件（含 BLE 协议栈），
                              以及去掉 IAR 运行时符号后的 ROM 符号表
  ti_ble5stack_freertos/      FreeRTOS 版 ICall：make_icall_port.py 从 SDK 源码生成 icall_POSIX.c，
                              附 icall_POSIX.c.patch 供审查
tools/                        跨平台脚本：flash.py、syscfg_gen.py
```

## 8. 和 TI 原始工程的区别

| 项目 | TI 原始工程 | 本仓库 |
|---|---|---|
| 构建 | 每种编译器一个 makefile，SysConfig 在编译时运行 | Bazel；SysConfig 输出提交进仓库 |
| 编译器 | 本机安装的 GCC / TI Clang / IAR | Pigweed 提供的 Arm GCC，三个系统相同 |
| FreeRTOS 内核 | SDK 里的 10.5.1，通过 `ti_freertos_config.c` 编译 | `@freertos` 11.3.2。TI 的 DPL、POSIX 层和启动代码仍然从 SDK 编译 |
| 主线程 | `pthread_create` | `pw::Thread` |
| idle/timer 任务内存、栈溢出钩子 | TI 的 `StaticAllocs_freertos.c` / `AppHooks` | Pigweed 的 `third_party/freertos:support`；栈溢出时会打印任务名 |
| `configASSERT` | 未定义 | 走 `pw_assert`，在串口打印文件名和行号 |
| UART | 未使用 | 新增 `CONFIG_UART2_0`（XDS110 虚拟串口），用于 `pw_log` |

## 9. 已知限制和注意事项

### TI 库从源码编译

GitHub 版 TI SDK 只提供 TI Drivers、driverlib、Display 的源码，所以这三个库由 Bazel 从源码编译：源码清单取自 TI 的 CMake 文件，编译选项和 TI 一致（`-std=c11 -O3`）。RF 驱动库和 BLE 协议栈库仍然用 TI 的预编译版本。

为此排除了：
- 需要额外第三方代码的驱动：CAN、SD/FatFS、PDM
- 依赖 grlib 的 LCD 显示驱动
- driverlib 的 `rfc.c`：RF 库里自带了一份

改为源码编译之后，在 LP_CC2652R7 上重新跑了 BLE 通信测试（42/42 条全部送达）。**LP_CC1312R7 的 TX/RX 还没有在硬件上重新验证。**

### BLE（LP_CC2652R7）

- **这是 TI 不支持的组合。** TI 只为 CC2652R7 提供了 TI-RTOS7（TI Clang/IAR）版本的 BLE 示例，本仓库把它移植到了 FreeRTOS + GCC：
  - 协议栈预编译库是 IAR 编译的，ABI 和 GCC 一致（枚举大小、`wchar_t` 都已核对）
  - ROM 里的协议栈函数通过 `--just-symbols` 链接，并去掉了 IAR 运行时符号 `__exit` 和 `__checksum*`
  - ICall 层用的是 SDK 里旧版的 FreeRTOS 实现，再按 TI-RTOS 版 `icall.c` 打了补丁，所有改动见 `make_icall_port.py`
  - 内存布局和 TI 官方构建一致：RAM 前 0xFE0 字节留给 ROM 协议栈，NV 区在 `0x48000`，ICall 堆从 `.bss` 结束处一直到栈底
- **ICall 移植中修掉的问题**：
  - ICall 堆没有初始化
  - 调度器启动前调用了 `pthread_self()`。TI 的 `FreeRTOSConfig.h` 没有定义 `configASSERT`，所以 TI 的构建里这个问题被掩盖了
  - 缺少 `ICall_registerAppCback`
- **升级 SDK 时**：
  - 重新运行 `python3 third_party/ti_ble5stack_freertos/make_icall_port.py`，如果 TI 改了源码，脚本会报错而不是悄悄出错
  - 用 `ble_led_link` 在两块板子上重新跑一遍通信测试
- **日志输出**：BLE 板的 `pw_log` 输出到 TI UART Display 的滚动区（菜单下方），因为 UART 已经被 Display 占用了。中断里或者关中断时（比如 `PW_CRASH`）会改为直接写 UART。`Display_open` 之前的日志会被丢弃。
- **调试注意**：用调试器 halt BLE 板会打乱射频时序，可能导致断线（主机会自动重新扫描和连接）。看运行时状态时，优先用不 halt 的内存读取。

- **FreeRTOS 版本**：TI 在 SDK 8.33 中验证的是 FreeRTOS 10.5.1，这里用的是 11.3.2（Pigweed 要求的版本）。已经在 LP_CC1312R7 上实测：调度器、tickless 待机、500 ms 周期的 RF 发送、RX 的同步接收（每个信标都收到，`PROP_DONE_OK`）都正常。TI DPL 为内核对象预留的空间也已核对过（`StaticSemaphore_t` 是 80 字节，正好等于 `SemaphoreP_STRUCT_SIZE`）。升级 SDK 或 Pigweed 后需要重新核对。
- **日志输出方式**：`pw_log` 写串口是轮询阻塞的（115200 波特率下大约 87 µs/字节），这样关中断时也能输出。时间敏感的代码路径里不要大量打日志。读串口（`pw::sys_io::ReadByte`）只能在任务上下文里调用。
- **预编译库列表**：`third_party/ti_simplelink/ti_sdk.BUILD.bazel` 里的 TI 预编译库列表要和 SysConfig 的要求一致。`tools/syscfg_gen.py` 运行时会打印出 SysConfig 要求的库列表，方便对照。
- **`pw_assert` 设为 `LOOP`**：断言失败后打印信息并原地停住。默认的 `ABORT` 会调用 `abort()`，而 Pigweed 工具链又把 `abort()` 包装成 `PW_CRASH`，结果是无限递归，会把栈吃穿。
- **升级 Pigweed**：修改 `MODULE.bazel` 里 `git_override` 的 `commit`，然后重新编译。
