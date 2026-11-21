"""Repository rule providing the TI SimpleLink CC13xx/CC26xx SDK.

Default: the SDK is fetched from TI's public GitHub repository
(TexasInstruments/simplelink-lowpower-f2-sdk) at a pinned tag, as a sparse
partial git clone containing only the directories this repo builds from
(~15 MB download). Nothing has to be installed by hand; only `git` is needed.

Override: to use a locally installed SDK (TI installer or a git checkout of
the same version), set TI_SIMPLELINK_SDK, e.g. in user.bazelrc:
    common --repo_env=TI_SIMPLELINK_SDK=/path/to/simplelink_cc13xx_cc26xx_sdk_8_33_00_16
"""

# Directories of the SDK used by the BUILD file (cone-mode sparse checkout;
# files directly inside parent directories, e.g. source/ti/devices/*.h, are
# included automatically).
_SPARSE_DIRS = [
    "kernel/freertos/dpl",
    "kernel/freertos/startup",
    "source/ti/ble5stack",
    "source/ti/bleapp",
    "source/ti/common",
    "source/ti/devices/cc13x2x7_cc26x2x7",
    "source/ti/display",
    "source/ti/drivers",
    "source/ti/log",
    "source/ti/posix",
]

def _git(rctx, git, args, what):
    res = rctx.execute([git] + args, timeout = 1200, quiet = True)
    if res.return_code != 0:
        fail("TI SDK: `git {}` failed ({}):\n{}{}".format(" ".join(args), what, res.stdout, res.stderr))

def _fetch_from_github(rctx):
    git = rctx.which("git")
    if not git:
        fail("TI SDK: `git` not found on PATH. Install Git (https://git-scm.com).")
    rctx.report_progress("Fetching TI SimpleLink SDK {} from GitHub (sparse)".format(rctx.attr.tag))
    _git(rctx, git, ["init", "-q", "."], "init")
    _git(rctx, git, ["remote", "add", "origin", rctx.attr.remote], "remote")
    _git(rctx, git, ["sparse-checkout", "set", "--cone"] + _SPARSE_DIRS, "sparse-checkout")
    _git(rctx, git, ["fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", "tag", rctx.attr.tag], "fetch")
    _git(rctx, git, ["-c", "advice.detachedHead=false", "checkout", "-q", "FETCH_HEAD"], "checkout")
    res = rctx.execute([git, "rev-parse", "FETCH_HEAD^{commit}"], quiet = True)
    if rctx.attr.commit and res.stdout.strip() != rctx.attr.commit:
        fail("TI SDK: tag {} resolved to {}, expected {}".format(rctx.attr.tag, res.stdout.strip(), rctx.attr.commit))
    rctx.delete(".git")

def _ti_simplelink_sdk_impl(rctx):
    local = rctx.getenv("TI_SIMPLELINK_SDK")
    if local:
        local = local.replace("\\", "/")
        if not rctx.path(local + "/source/ti").exists:
            fail("TI_SIMPLELINK_SDK={} does not look like a TI SimpleLink SDK (no source/ti).".format(local))
        rctx.symlink(local + "/source", "source")
        rctx.symlink(local + "/kernel", "kernel")
    else:
        _fetch_from_github(rctx)
    rctx.symlink(rctx.attr.build_file, "BUILD.bazel")

ti_simplelink_sdk = repository_rule(
    implementation = _ti_simplelink_sdk_impl,
    attrs = {
        "build_file": attr.label(allow_single_file = True, mandatory = True),
        "commit": attr.string(doc = "Expected commit of `tag` (integrity check)."),
        "remote": attr.string(default = "https://github.com/TexasInstruments/simplelink-lowpower-f2-sdk.git"),
        "tag": attr.string(mandatory = True),
    },
    doc = "TI SimpleLink CC13xx/CC26xx SDK (sparse clone from GitHub, or a local install).",
)
