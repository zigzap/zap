const std = @import("std");

pub fn build_facilio(
    comptime subdir: []const u8,
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    use_openssl: bool,
) !*std.Build.Step.Compile {
    const lib = b.addStaticLibrary(.{
        .name = "facil.io",
        .target = target,
        .optimize = optimize,
    });

    // Generate flags
    var flags = std.ArrayList([]const u8).init(std.heap.page_allocator);
    if (optimize != .Debug) try flags.append("-Os");
    try flags.append("-Wno-return-type-c-linkage");
    try flags.append("-fno-sanitize=undefined");

    //
    // let's not override malloc from within the lib
    // when used as lib, not sure if it would work as expected anyway
    // try flags.append("-DFIO_OVERRIDE_MALLOC");
    //

    try flags.append("-DFIO_HTTP_EXACT_LOGGING");
    if (target.result.abi == .musl)
        try flags.append("-D_LARGEFILE64_SOURCE");
    if (use_openssl)
        try flags.append("-DHAVE_OPENSSL -DFIO_TLS_FOUND");

    // Include paths
    lib.addIncludePath(b.path(subdir ++ "/."));
    lib.addIncludePath(b.path(subdir ++ "/lib/facil"));
    lib.addIncludePath(b.path(subdir ++ "/lib/facil/fiobj"));
    lib.addIncludePath(b.path(subdir ++ "/lib/facil/cli"));
    lib.addIncludePath(b.path(subdir ++ "/lib/facil/http"));
    lib.addIncludePath(b.path(subdir ++ "/lib/facil/http/parsers"));
    if (use_openssl)
        lib.addIncludePath(b.path(subdir ++ "/lib/facil/tls"));

    // C source files
    lib.addCSourceFiles(.{
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
        lib.addCSourceFiles(.{
            .files = &.{
                subdir ++ "/lib/facil/tls/fio_tls_openssl.c",
                subdir ++ "/lib/facil/tls/fio_tls_missing.c",
            },
            .flags = flags.items,
        });
    }

    // link against libc
    lib.linkLibC();

    // link in libopenssl and libcrypto on demand
    if (use_openssl) {
        lib.linkSystemLibrary("ssl");
        lib.linkSystemLibrary("crypto");
    }

    b.installArtifact(lib);

    return lib;
}
