#!/usr/bin/env python3
"""Run Bazel for this repo without installing anything first.

Usage:  python3 tools/bazel.py build //...          (Windows: python tools\\bazel.py ...)

Uses `bazelisk` from PATH if present. Otherwise downloads a pinned bazelisk
release for this OS/CPU into ~/.cache/rf_sync_pw/ (verified by SHA-256) and
runs it. Bazelisk then downloads the Bazel version in .bazelversion, and Bazel
downloads everything else (Arm GCC, Pigweed, FreeRTOS, the TI SDK).
"""

import hashlib
import os
import platform
import shutil
import stat
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BAZELISK_VERSION = "v1.30.0"
BAZELISK_SHA256 = {
    "bazelisk-darwin-amd64": "a0e6468b0c6b812f782d8cbfb05824121f6980c0bd70aaa1774fb8272bb7b2ad",
    "bazelisk-darwin-arm64": "555d2ff912ccd336d0060bdb5251ef2f62629c6f8fbadb59005a45d5625793ec",
    "bazelisk-linux-amd64": "a248ae2b2b62b5baebb748f6fba7203bff1a27e5d01b55399a8406744cb539e8",
    "bazelisk-linux-arm64": "b2bcc040c213b44586b121bda6d983c35df121b917c3a7a66fb16f2fa0df1a4f",
    "bazelisk-windows-amd64.exe": "5d14a31e9d8a363c48ba1900dbd56764d730835232eb0d113cce255856ffe3ef",
    "bazelisk-windows-arm64.exe": "cb832b1d157e0dc5f89108da426f9c6fe087f5b15873e072480ec49cc5afe488",
}


def _asset_name() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    arch = "arm64" if machine in ("arm64", "aarch64") else "amd64"
    if system == "windows":
        return f"bazelisk-windows-{arch}.exe"
    if system == "darwin":
        return f"bazelisk-darwin-{arch}"
    return f"bazelisk-linux-{arch}"


def bazelisk() -> str:
    """Path to a bazelisk executable (from PATH or downloaded and cached)."""
    for name in ("bazelisk", "bazel"):
        exe = shutil.which(name)
        if exe:
            return exe
    asset = _asset_name()
    cache = Path(os.environ.get("RF_SYNC_PW_CACHE", Path.home() / ".cache" / "rf_sync_pw"))
    exe = cache / f"{BAZELISK_VERSION}-{asset}"
    if exe.exists():
        return str(exe)
    cache.mkdir(parents=True, exist_ok=True)
    url = f"https://github.com/bazelbuild/bazelisk/releases/download/{BAZELISK_VERSION}/{asset}"
    print(f"Downloading {url}", file=sys.stderr)
    data = urllib.request.urlopen(url, timeout=120).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != BAZELISK_SHA256[asset]:
        raise SystemExit(f"SHA-256 mismatch for {asset}: got {digest}")
    tmp = exe.with_suffix(".tmp")
    tmp.write_bytes(data)
    tmp.chmod(tmp.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    tmp.replace(exe)
    return str(exe)


def main() -> int:
    return subprocess.call([bazelisk()] + sys.argv[1:], cwd=REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main())
