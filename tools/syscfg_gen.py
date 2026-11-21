#!/usr/bin/env python3
"""Regenerate the checked-in SysConfig output for an app.

Usage:  python tools/syscfg_gen.py apps/rf_sync_tx          # regenerate
        python tools/syscfg_gen.py apps/rf_sync_rx --gui    # open the GUI
        python tools/syscfg_gen.py --all                    # every app

Each app directory holds exactly one *.syscfg; its output is written to
<app>/syscfg_gen/ and committed, so `bazelisk build` never needs SysConfig.
Every app's .syscfg must contain a UART2 instance named CONFIG_UART2_0
(used by pw_log; see README.md).

Files TI generates that Bazel handles differently are not copied:
  * ti_freertos_config.c / ti_freertos_portable_config.c: these #include the
    FreeRTOS kernel; the kernel comes from Bazel's @freertos module and the
    TI DPL/POSIX sources are compiled in @ti_simplelink_sdk.
  * ti_utils_build_linker.cmd.genlibs: the libraries are listed in
    third_party/ti_simplelink/ti_sdk.BUILD.bazel (printed below for review).
  * FreeRTOSConfig.h: the kernel is shared by all apps on a board, so it is
    written once per board to targets/<board>/config/ti_syscfg_FreeRTOSConfig.h
    (the board comes from the "@cliArgs --board /ti/boards/<BOARD>" line of
    the .syscfg). This script fails if two apps for the same board disagree.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ti_paths  # noqa: E402

REPO = ti_paths.REPO_ROOT


def board_freertos_config(syscfg: Path) -> Path:
    """targets/<board>/config/ti_syscfg_FreeRTOSConfig.h for this .syscfg."""
    m = re.search(r"@cliArgs.*--board\s+/ti/boards/(\S+)", syscfg.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"{syscfg}: no '@cliArgs --board /ti/boards/<BOARD>' line")
    target = REPO / "targets" / m.group(1).lower()
    if not target.is_dir():
        raise SystemExit(f"{syscfg}: board {m.group(1)} has no {target.relative_to(REPO)} directory")
    return target / "config" / "ti_syscfg_FreeRTOSConfig.h"

SKIP = {
    "ti_freertos_config.c",
    "ti_freertos_portable_config.c",
    "ti_utils_build_linker.cmd.genlibs",
    "ti_utils_build_linker.cmd.genmap",
    "syscfg_c.rov.xs",
    "FreeRTOSConfig.h",
}


def find_syscfg(app_dir: Path) -> Path:
    files = sorted(app_dir.glob("*.syscfg"))
    if len(files) != 1:
        raise SystemExit(f"{app_dir}: expected exactly one .syscfg file, found {len(files)}")
    return files[0]


def generate(app_dir: Path, cli: Path, product: Path, force_freertos: bool) -> int:
    syscfg = find_syscfg(app_dir)
    if "CONFIG_UART2_0" not in syscfg.read_text(encoding="utf-8"):
        print(f"warning: {syscfg.name} has no CONFIG_UART2_0; pw_log will not link.")

    with tempfile.TemporaryDirectory() as tmp:
        cmd = [str(cli), "--compiler", "gcc", "--product", str(product),
               "--output", tmp, str(syscfg)]
        print("Running:", " ".join(cmd))
        rc = subprocess.call(cmd, shell=ti_paths.IS_WINDOWS)
        if rc != 0:
            return rc

        # The FreeRTOS config is board-wide: make sure apps agree on it.
        BOARD_FREERTOS_CONFIG = board_freertos_config(syscfg)
        BOARD_FREERTOS_CONFIG.parent.mkdir(parents=True, exist_ok=True)
        new_cfg = (Path(tmp) / "FreeRTOSConfig.h").read_bytes()
        if BOARD_FREERTOS_CONFIG.exists() and BOARD_FREERTOS_CONFIG.read_bytes() != new_cfg:
            if not force_freertos:
                print(f"error: {syscfg.name} generates a FreeRTOSConfig.h that differs from "
                      f"{BOARD_FREERTOS_CONFIG.relative_to(REPO)}.\n"
                      "All apps on a board share one kernel config. Align the FreeRTOS "
                      "settings in the .syscfg files, or rerun with --update-freertos-config "
                      "(then regenerate the other apps).")
                return 1
            print("  updating board FreeRTOS config")
        BOARD_FREERTOS_CONFIG.write_bytes(new_cfg)

        out_dir = app_dir / "syscfg_gen"
        out_dir.mkdir(exist_ok=True)
        for old in out_dir.iterdir():
            if old.is_file():
                old.unlink()
        for f in sorted(Path(tmp).iterdir()):
            if f.name in SKIP:
                continue
            shutil.copyfile(f, out_dir / f.name)
            print("  ->", (out_dir / f.name).relative_to(REPO))

        genlibs = Path(tmp) / "ti_utils_build_linker.cmd.genlibs"
        if genlibs.exists():
            libs = [l.strip() for l in genlibs.read_text().splitlines() if l.strip().startswith('"')]
            print("  Libraries requested by SysConfig (keep ti_sdk.BUILD.bazel in sync):")
            for lib in libs:
                print("     ", lib)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("app", nargs="?", help="app directory, e.g. apps/rf_sync_tx")
    ap.add_argument("--all", action="store_true", help="regenerate every app")
    ap.add_argument("--gui", action="store_true", help="open the SysConfig GUI")
    ap.add_argument("--update-freertos-config", action="store_true",
                    help="allow this app to change the board-wide FreeRTOS config")
    args = ap.parse_args()

    if args.all:
        apps = sorted(p.parent for p in (REPO / "apps").glob("*/*.syscfg"))
    elif args.app:
        apps = [(Path.cwd() / args.app).resolve()]
    else:
        ap.error("give an app directory or --all")

    sdk = ti_paths.find_sdk()
    cli = ti_paths.find_sysconfig()
    product = sdk / ".metadata" / "product.json"

    if args.gui:
        gui = cli.with_name(cli.name.replace("sysconfig_cli", "sysconfig_gui"))
        cmd = [str(gui), "--compiler", "gcc", "--product", str(product), str(find_syscfg(apps[0]))]
        return subprocess.call(cmd, shell=ti_paths.IS_WINDOWS)

    for app in apps:
        rc = generate(app, cli, product, args.update_freertos_config)
        if rc != 0:
            return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())
