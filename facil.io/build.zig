const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const use_openssl = b.option(bool, "use_openssl", "Use OpenSSL") orelse false;

    const upstream = b.dependency("facil.io", .{ .target = target, .optimize = optimize });

    const lib = try build_facilio(b, upstream, target, optimize, use_openssl);

    b.installArtifact(lib);
}

pub fn build_facilio(
    b: *std.Build,
    upstream: *std.Build.Dependency,
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
    var flags = std.ArrayList([]const u8).init(b.allocator);
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
    lib.addIncludePath(upstream.path("."));
    lib.addIncludePath(upstream.path("lib/facil"));
    lib.addIncludePath(upstream.path("lib/facil/fiobj"));
    lib.addIncludePath(upstream.path("lib/facil/cli"));
    lib.addIncludePath(upstream.path("lib/facil/http"));
    lib.addIncludePath(upstream.path("lib/facil/http/parsers"));
    if (use_openssl)
        lib.addIncludePath(upstream.path("lib/facil/tls"));

    lib.addCSourceFiles(.{
        .root = b.path("."),
        .files = &.{
            "src/fio_zig.c",
        },
        .flags = flags.items,
    });

    // C source files
    lib.addCSourceFiles(.{
        .root = upstream.path("."),
        .files = &.{
            "lib/facil/fio.c",
            "lib/facil/http/http.c",
            "lib/facil/http/http1.c",
            "lib/facil/http/websockets.c",
            "lib/facil/http/http_internal.c",
            "lib/facil/fiobj/fiobj_numbers.c",
            "lib/facil/fiobj/fio_siphash.c",
            "lib/facil/fiobj/fiobj_str.c",
            "lib/facil/fiobj/fiobj_ary.c",
            "lib/facil/fiobj/fiobj_data.c",
            "lib/facil/fiobj/fiobj_hash.c",
            "lib/facil/fiobj/fiobj_json.c",
            "lib/facil/fiobj/fiobject.c",
            "lib/facil/fiobj/fiobj_mustache.c",
            "lib/facil/cli/fio_cli.c",
        },
        .flags = flags.items,
    });

    if (use_openssl) {
        lib.addCSourceFiles(.{
            .root = upstream.path("."),
            .files = &.{
                "lib/facil/tls/fio_tls_openssl.c",
                "lib/facil/tls/fio_tls_missing.c",
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

    return lib;
}
