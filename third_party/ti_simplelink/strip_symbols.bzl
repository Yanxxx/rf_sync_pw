"""strip_symbols(): copy an ELF file without some symbols (toolchain objcopy).

Runs the C++ toolchain's objcopy directly (no shell), so it works on Linux,
macOS and Windows.
"""

load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

def _strip_symbols_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    objcopy = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = ACTION_NAMES.objcopy_embed_data,
    )
    args = ctx.actions.args()
    for sym in ctx.attr.symbols:
        args.add("--strip-symbol=" + sym)
    args.add(ctx.file.src)
    args.add(ctx.outputs.out)
    ctx.actions.run(
        executable = objcopy,
        arguments = [args],
        inputs = depset([ctx.file.src], transitive = [cc_toolchain.all_files]),
        outputs = [ctx.outputs.out],
        mnemonic = "StripSymbols",
        progress_message = "Stripping symbols from %{input}",
    )
    return [DefaultInfo(files = depset([ctx.outputs.out]))]

strip_symbols = rule(
    implementation = _strip_symbols_impl,
    attrs = {
        "out": attr.output(mandatory = True),
        "src": attr.label(allow_single_file = True, mandatory = True),
        "symbols": attr.string_list(mandatory = True),
    },
    toolchains = use_cpp_toolchain(),
    fragments = ["cpp"],
)
