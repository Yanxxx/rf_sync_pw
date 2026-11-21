"""lp_cc2652r7_ble_app(): build a TI BLE5-stack app for LP_CC2652R7."""

load("@pigweed//pw_build:binary_tools.bzl", "pw_elf_to_bin")
load("@pigweed//pw_build:pw_cc_binary.bzl", "pw_cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

_MCU_ONLY = [Label("@ti_simplelink_sdk//:cc26x2x7")]

_BUILD_COMPONENTS_OPT = Label("@ti_simplelink_sdk//:source/ti/ble5stack/config/build_components.opt")
_FACTORY_CONFIG_OPT = Label("@ti_simplelink_sdk//:source/ti/ble5stack/config/factory_config.opt")

# Defines from TI's basic_ble makefile, plus the FreeRTOS ones TI uses for
# its FreeRTOS BLE examples (FREERTOS, NVOCMP_POSIX_MUTEX).
_BLE_DEFINES = [
    "ICALL_NO_APP_EVENTS",
    "FLASH_ROM_BUILD",
    "NVOCMP_NWSAMEITEM=1",
    "HEAPMGR_CONFIG=0x80",
    "HEAPMGR_SIZE=0x0",
    "FREERTOS",
    "NVOCMP_POSIX_MUTEX",
]

def lp_cc2652r7_ble_app(
        name,
        srcs,
        includes = [],
        app_name = None,
        app_defines = [],
        app_deps = [],
        syscfg_dir = "syscfg_gen",
        stack_srcs = None):
    """Defines <name>.elf and <name>.bin for a BLE5-stack app on LP_CC2652R7.

    Args:
      name: Target name prefix.
      srcs: App sources, including main() (see apps/basic_ble/startup/main.c).
      includes: App include directories.
      app_name: Name printed at boot (defaults to `name`).
      app_defines: Extra preprocessor defines for the app sources.
      app_deps: Extra deps for the app (e.g. C++ libraries using Pigweed).
      syscfg_dir: Directory with this app's SysConfig output (FreeRTOS, GCC).
      stack_srcs: SDK stack/glue sources to compile with the app. Defaults to
        the set used by TI's basic_ble.
    """
    app_name = app_name or name
    opt = syscfg_dir + "/ti_utils_build_compiler.opt"

    # Same response files as TI's makefile: SysConfig's compiler options and
    # the stack's feature configuration (all -D options).
    opt_inputs = [opt, _BUILD_COMPONENTS_OPT, _FACTORY_CONFIG_OPT]
    opt_copts = [
        "@$(location {})".format(opt),
        "@$(location {})".format(_BUILD_COMPONENTS_OPT),
        "@$(location {})".format(_FACTORY_CONFIG_OPT),
    ]

    # TI code is not written for Pigweed's -Werror warning set.
    ti_copts = ["-std=c99", "-w"] + opt_copts  # TI-POSIX requires strict c99

    if stack_srcs == None:
        stack_srcs = [
            Label("@ti_simplelink_sdk//:ble5stack_icall_srcs"),
            Label("@ti_simplelink_sdk//:bleapp_util_srcs"),
            Label("@ti_simplelink_sdk//:bleapp_menu_srcs"),
            Label("@ti_simplelink_sdk//:bleapp_profile_srcs"),
            Label("@ti_simplelink_sdk//:nv_srcs"),
            Label("//third_party/ti_ble5stack_freertos:icall_POSIX.c"),
        ]

    # App headers: SysConfig's ti_ble_config.c includes app_main.h.
    cc_library(
        name = name + "_app_headers",
        hdrs = [s for s in srcs if s.endswith(".h")],
        includes = includes,
    )

    # SysConfig output for this app (pins, radio, BLE config, CCFG).
    cc_library(
        name = name + "_syscfg",
        srcs = native.glob([syscfg_dir + "/*.c"], allow_empty = False),
        hdrs = native.glob([syscfg_dir + "/*.h"], allow_empty = False),
        additional_compiler_inputs = opt_inputs,
        copts = ti_copts,
        defines = _BLE_DEFINES,
        includes = [syscfg_dir],
        target_compatible_with = _MCU_ONLY,
        deps = [
            ":" + name + "_app_headers",
            Label("@ti_simplelink_sdk//:ble5stack_headers"),
        ],
        alwayslink = True,
    )

    # ICall (FreeRTOS port), stack config, BLEAppUtil, MenuModule, profiles
    # and NV, compiled with this app's configuration. alwayslink because the
    # prebuilt stack (linked later in the command line) calls into them.
    cc_library(
        name = name + "_stack",
        srcs = stack_srcs,
        additional_compiler_inputs = opt_inputs,
        copts = ti_copts,
        target_compatible_with = _MCU_ONLY,
        deps = [
            ":" + name + "_syscfg",
            Label("@ti_simplelink_sdk//:ble5stack_headers"),
            Label("@ti_simplelink_sdk//:ble5stack_libs"),
            Label("//third_party/ti_simplelink:ble_rom_symbols"),
            "@freertos",
        ],
        alwayslink = True,
    )

    # App code incl. main(). (cc_binary has no additional_compiler_inputs,
    # so it is compiled here and the binary only links.)
    cc_library(
        name = name + "_app",
        srcs = srcs,
        additional_compiler_inputs = opt_inputs,
        copts = ti_copts,
        includes = includes,
        local_defines = ['APP_NAME=\\"{}\\"'.format(app_name)] + app_defines,
        target_compatible_with = _MCU_ONLY,
        deps = app_deps + [
            ":" + name + "_stack",
            ":" + name + "_syscfg",
            "@freertos",
            "@pigweed//pw_assert:check",
            "@pigweed//pw_log",
            Label("@ti_simplelink_sdk//:ble5stack_headers"),
        ],
        alwayslink = True,
    )

    pw_cc_binary(
        name = name + ".elf",
        target_compatible_with = _MCU_ONLY,
        deps = [
            ":" + name + "_app",
            Label("//targets/lp_cc2652r7:board"),
        ],
    )

    pw_elf_to_bin(
        name = name + ".bin",
        bin_out = name + ".bin",
        elf_input = ":" + name + ".elf",
        target_compatible_with = _MCU_ONLY,
    )
