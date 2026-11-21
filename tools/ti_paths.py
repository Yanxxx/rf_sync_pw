"""Locate TI tools (SimpleLink SDK, SysConfig, UniFlash) on Linux/macOS/Windows.

Lookup order for each tool:
  1. Environment variable (TI_SIMPLELINK_SDK / TI_SYSCONFIG / TI_UNIFLASH).
  2. `user.bazelrc` line of the form `common --repo_env=TI_SIMPLELINK_SDK=...`
     (SDK only, so scripts and Bazel agree on one setting).
  3. Default TI install locations for the current OS.
"""

from __future__ import annotations

import glob
import os
import platform
import re
from pathlib import Path

SDK_VERSION = "8_33_00_16"
SDK_DIRNAME = f"simplelink_cc13xx_cc26xx_sdk_{SDK_VERSION}"
REPO_ROOT = Path(__file__).resolve().parent.parent

IS_WINDOWS = platform.system() == "Windows"
IS_MAC = platform.system() == "Darwin"


def _ti_roots() -> list[Path]:
    roots = [Path.home() / "ti"]
    if IS_WINDOWS:
        roots.insert(0, Path("C:/ti"))
    if IS_MAC:
        roots.append(Path("/Applications/ti"))
    return roots


def _from_user_bazelrc(var: str) -> str | None:
    rc = REPO_ROOT / "user.bazelrc"
    if not rc.exists():
        return None
    m = re.search(rf"--repo_env={var}=(\S+)", rc.read_text(encoding="utf-8"))
    return m.group(1).strip("\"'") if m else None


def _newest(pattern_parts: list[str]) -> Path | None:
    hits: list[str] = []
    for root in _ti_roots():
        hits += glob.glob(str(root.joinpath(*pattern_parts)))
    return Path(sorted(hits)[-1]) if hits else None


def find_sdk() -> Path:
    val = os.environ.get("TI_SIMPLELINK_SDK") or _from_user_bazelrc("TI_SIMPLELINK_SDK")
    if val:
        p = Path(val).expanduser()
    else:
        p = next((r / SDK_DIRNAME for r in _ti_roots() if (r / SDK_DIRNAME).is_dir()), None)
    if not p or not (p / "source" / "ti").is_dir():
        raise SystemExit(
            f"TI SimpleLink SDK not found. Install {SDK_DIRNAME} and set TI_SIMPLELINK_SDK "
            "(see README.md)."
        )
    return p


def find_sysconfig() -> Path:
    name = "sysconfig_cli.bat" if IS_WINDOWS else "sysconfig_cli.sh"
    val = os.environ.get("TI_SYSCONFIG")
    p = Path(val).expanduser() if val else _newest(["sysconfig_*", name])
    if p and p.is_dir():
        p = p / name
    if not p or not p.is_file():
        raise SystemExit("SysConfig not found. Install TI SysConfig and/or set TI_SYSCONFIG.")
    return p


def find_uniflash() -> Path:
    name = "dslite.bat" if IS_WINDOWS else "dslite.sh"
    val = os.environ.get("TI_UNIFLASH")
    p = Path(val).expanduser() if val else _newest(["uniflash_*", name])
    if p and p.is_dir():
        p = p / name
    if not p or not p.is_file():
        raise SystemExit("TI UniFlash not found. Install it from https://www.ti.com/tool/UNIFLASH\n"
                         "(default location under ~/ti or C:/ti), or set TI_UNIFLASH to dslite.sh/dslite.bat.")
    return p
