const std = @import("std");

pub fn build_facilio(
    comptime subdir: []const u8,
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    use_openssl: bool,
) !*std.Build.Step.Compile {
    const mod = b.addModule("facil.io", .{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    const lib = b.addLibrary(.{
        .name = "facil.io",
        .root_module = mod,
        .linkage = .dynamic,
    });

    // Generate flags
    var flags = std.ArrayList([]const u8).empty;
    defer flags.deinit(b.allocator);
    if (optimize != .Debug) try flags.append(b.allocator, "-Os");
    try flags.append(b.allocator, "-Wno-return-type-c-linkage");
    try flags.append(b.allocator, "-fno-sanitize=undefined");

    //
    // let's not override malloc from within the lib
    // when used as lib, not sure if it would work as expected anyway
    // try flags.append("-DFIO_OVERRIDE_MALLOC");
    //

    try flags.append(b.allocator, "-DFIO_HTTP_EXACT_LOGGING");
    if (target.result.abi == .musl)
        try flags.append(b.allocator, "-D_LARGEFILE64_SOURCE");
    if (use_openssl)
        try flags.append(b.allocator, "-DHAVE_OPENSSL -DFIO_TLS_FOUND");

    // Include paths
    mod.addIncludePath(b.path(subdir ++ "/."));
    mod.addIncludePath(b.path(subdir ++ "/lib/facil"));
    mod.addIncludePath(b.path(subdir ++ "/lib/facil/fiobj"));
    mod.addIncludePath(b.path(subdir ++ "/lib/facil/cli"));
    mod.addIncludePath(b.path(subdir ++ "/lib/facil/http"));
    mod.addIncludePath(b.path(subdir ++ "/lib/facil/http/parsers"));
    if (use_openssl)
        mod.addIncludePath(b.path(subdir ++ "/lib/facil/tls"));

    // C source files
    mod.addCSourceFiles(.{
        .files = &.{
            subdir ++ "/lib/facil/fio.c",
            subdir ++ "/lib/facil/fio_zig.c",
            subdir ++ "/lib/facil/http/http.c",
            subdir ++ "/lib/facil/http/http1.c",
            subdir ++ "/lib/facil/http/websockets.c",
            subdir ++ "/lib/facil/http/http_internal.c",
            subdir ++ "/lib/facil/fiobj/fiobj_numbers.c",
            subdir ++ "/lib/facil/fiobj/fio_siphash.c",
            subdir ++ "/lib/facil/fiobj/fiobj_str.c",
            subdir ++ "/lib/facil/fiobj/fiobj_ary.c",
            subdir ++ "/lib/facil/fiobj/fiobj_data.c",
            subdir ++ "/lib/facil/fiobj/fiobj_hash.c",
            subdir ++ "/lib/facil/fiobj/fiobj_json.c",
            subdir ++ "/lib/facil/fiobj/fiobject.c",
            subdir ++ "/lib/facil/fiobj/fiobj_mustache.c",
            subdir ++ "/lib/facil/cli/fio_cli.c",
        },
        .flags = flags.items,
    });

    if (use_openssl) {
        mod.addCSourceFiles(.{
            .files = &.{
                subdir ++ "/lib/facil/tls/fio_tls_openssl.c",
                subdir ++ "/lib/facil/tls/fio_tls_missing.c",
            },
            .flags = flags.items,
        });
    }

    // link in modopenssl and libcrypto on demand
    if (use_openssl) {
        mod.linkSystemLibrary("ssl", .{});
        mod.linkSystemLibrary("crypto", .{});
    }

    b.installArtifact(lib);

    return lib;
}
