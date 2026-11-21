#!/usr/bin/env python3
"""Build an app and flash it to a LaunchPad (LP_CC1312R7, LP_CC2652R7) via XDS110 (TI UniFlash).

Usage:
  python tools/flash.py rf_sync_tx                      # one board connected
  python tools/flash.py rf_sync_rx --serial <SERIAL>    # pick the board
  python tools/flash.py ble_led_link:ble_led_central --serial <SERIAL>
                                                        # <app dir>:<target>
  python tools/flash.py --list                          # list XDS110 probes
  python tools/flash.py rf_sync_tx --no-build           # flash last build

With more than one LaunchPad connected, --serial is required (the XDS110
serial number is printed by --list and on the sticker under the board).
The board (Bazel platform and .ccxml) is taken from the app's .syscfg
("@cliArgs --board /ti/boards/<BOARD>" -> targets/<board>/).
Works on Linux, macOS and Windows. Needs TI UniFlash (found under the default
TI install dir, or set TI_UNIFLASH to dslite.sh / dslite.bat).
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

CCXML = ti_paths.REPO_ROOT / "targets" / "lp_cc1312r7" / "CC1312R7.ccxml"


def resolve_app(app: str) -> tuple[str, str, Path]:
    """'pkg:name' or 'name' -> (bazel target, board dir name, elf path)."""
    pkg, _, name = app.partition(":")
    name = name or pkg
    app_dir = ti_paths.REPO_ROOT / "apps" / pkg
    syscfgs = list(app_dir.glob("*.syscfg"))
    if not syscfgs:
        raise SystemExit(f"{app_dir} has no .syscfg (unknown app '{app}')")
    m = re.search(r"--board\s+/ti/boards/(\S+)", syscfgs[0].read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"{syscfgs[0]}: no '@cliArgs --board' line")
    board = m.group(1).lower()
    elf = ti_paths.REPO_ROOT / "bazel-bin" / "apps" / pkg / f"{name}.elf"
    return f"//apps/{pkg}:{name}.elf", board, elf


from bazel import bazelisk  # noqa: E402  (downloads bazelisk if needed)


def xdsdfu() -> Path:
    base = ti_paths.find_uniflash().parent
    for p in base.glob("deskdb/content/TICloudAgent/*/ccs_base/common/uscif/xds110/xdsdfu*"):
        if p.suffix in ("", ".exe"):
            return p
    raise SystemExit("xdsdfu not found in the UniFlash installation.")


def list_probes() -> list[str]:
    out = subprocess.run([str(xdsdfu()), "-e"], capture_output=True, text=True).stdout
    return re.findall(r"Serial Num:\s+(\S+)", out)


def ccxml_for_serial(serial: str, tmpdir: Path, base: Path = CCXML) -> Path:
    """Copy the board ccxml, switching probe selection to 'by serial number'.

    In TIXDS110_Connection.xml, choice 0 is "Only one XDS110 installed" and
    choice 1 is "Select by serial number".
    """
    xml = base.read_text(encoding="utf-8")
    xml, n1 = re.subn(
        r'(<property Type="choicelist" Value=")\d+(" id="Debug Probe Selection">)',
        r"\g<1>1\g<2>", xml)
    xml, n2 = re.subn(
        r'(<property Type="stringfield" Value=")[^"]*(" id="-- Enter the serial number"/>)',
        rf"\g<1>{serial}\g<2>", xml)
    if n1 != 1 or n2 != 1:
        raise SystemExit(f"Could not set the probe serial number in {base.name}.")
    out = tmpdir / f"CC1312R7_{serial}.ccxml"
    out.write_text(xml, encoding="utf-8")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("app", nargs="?",
                    help="app, e.g. rf_sync_tx or ble_led_link:ble_led_central")
    ap.add_argument("--serial", help="XDS110 serial number of the board to flash")
    ap.add_argument("--list", action="store_true", help="list connected XDS110 probes")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()
    args.serial = (args.serial or "").strip() or None  # empty = single board

    if args.list:
        for s in list_probes():
            print(s)
        return 0
    if not args.app:
        ap.error("give an app name (e.g. rf_sync_tx) or --list")

    target, board, elf = resolve_app(args.app)
    board_dir = ti_paths.REPO_ROOT / "targets" / board
    base_ccxml = next(board_dir.glob("*.ccxml"))
    if not args.no_build:
        rc = subprocess.call([bazelisk(), "build", f"--platforms=//targets/{board}", target],
                             cwd=ti_paths.REPO_ROOT)
        if rc != 0:
            return rc

    if not elf.exists():
        raise SystemExit(f"{elf} not found; build it first.")

    probes = list_probes()
    if args.serial and args.serial not in probes:
        raise SystemExit(f"XDS110 {args.serial} not connected (found: {', '.join(probes) or 'none'}).")
    if not args.serial and len(probes) > 1:
        raise SystemExit(f"Several XDS110 probes connected ({', '.join(probes)}); use --serial.")

    with tempfile.TemporaryDirectory() as tmp:
        ccxml = ccxml_for_serial(args.serial, Path(tmp), base_ccxml) if args.serial else base_ccxml
        # --reset 2 = Board Reset (XDS110 pulses nRESET and disconnects), so
        # the firmware boots normally; otherwise the CPU stays halted.
        cmd = [str(ti_paths.find_uniflash()), f"--config={ccxml}", "--flash", "--verify",
               "--reset", "2", str(elf)]
        print("Running:", " ".join(cmd))
        return subprocess.call(cmd, shell=ti_paths.IS_WINDOWS)


if __name__ == "__main__":
    sys.exit(main())
