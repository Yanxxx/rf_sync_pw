"""lp_cc1312r7_app(): build a TI-style (mainThread) app for LP_CC1312R7."""

load("@pigweed//pw_build:binary_tools.bzl", "pw_elf_to_bin")
load("@pigweed//pw_build:pw_cc_binary.bzl", "pw_cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Apps built by this macro target the CC13x2x7 family (LP_CC1312R7).
_MCU_ONLY = [Label("@ti_simplelink_sdk//:cc13x2x7")]

# TI example code is not written for Pigweed's -Werror warning set.
_TI_APP_COPTS = [
    "-std=c99",
    "-Wno-unused-parameter",
    "-Wno-sign-compare",
    "-Wno-undef",
    "-Wno-strict-prototypes",
]

def lp_cc1312r7_app(name, srcs, app_name = None, deps = [], syscfg_dir = "syscfg_gen"):
    """Defines <name>.elf and <name>.bin for an app on LP_CC1312R7.

    The app provides `void *mainThread(void *arg0)` (as in TI's examples);
    //apps/common:main.cc supplies main(), board init, pw_log and the
    pw::Thread that runs mainThread.

    Args:
      name: Target name prefix.
      srcs: App C/C++ sources.
      app_name: Name printed at boot (defaults to `name`).
      deps: Extra deps for the app sources.
      syscfg_dir: Directory with this app's SysConfig output
        (written by `python tools/syscfg_gen.py <app dir>`).
    """
    app_name = app_name or name

    # SysConfig output for this app (pins, radio config, CCFG). Generated TI
    # code is compiled like third-party code.
    cc_library(
        name = name + "_syscfg",
        srcs = native.glob([syscfg_dir + "/*.c"], allow_empty = False),
        hdrs = native.glob([syscfg_dir + "/*.h"], allow_empty = False),
        copts = ["-std=c99", "-w"],
        includes = [syscfg_dir],
        target_compatible_with = _MCU_ONLY,
        deps = ["@ti_simplelink_sdk//:headers"],
        alwayslink = True,
    )

    cc_library(
        name = name + "_lib",
        srcs = srcs,
        copts = _TI_APP_COPTS,
        target_compatible_with = _MCU_ONLY,
        deps = [
            ":" + name + "_syscfg",
            "@ti_simplelink_sdk//:headers",
            "@ti_simplelink_sdk//:posix_headers",
        ] + deps,
    )

    pw_cc_binary(
        name = name + ".elf",
        srcs = [Label("//apps/common:main.cc")],
        local_defines = ['APP_NAME=\\"{}\\"'.format(app_name)],
        target_compatible_with = _MCU_ONLY,
        deps = [
            ":" + name + "_lib",
            Label("//targets/lp_cc1312r7:board"),
            "@freertos",
            "@pigweed//pw_log",
            "@pigweed//pw_preprocessor",
            "@pigweed//pw_thread:thread",
            "@pigweed//pw_thread_freertos:thread",
            "@ti_simplelink_sdk//:headers",
        ],
    )

    pw_elf_to_bin(
        name = name + ".bin",
        bin_out = name + ".bin",
        elf_input = ":" + name + ".elf",
        target_compatible_with = _MCU_ONLY,
    )
