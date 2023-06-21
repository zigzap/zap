const std = @import("std");
const builtin = std.builtin;
const assert = std.debug.assert;
const io = std.io;
const fs = std.fs;
const mem = std.mem;
const process = std.process;
const Allocator = mem.Allocator;
const ThreadPool = std.Thread.Pool;
const WaitGroup = std.Thread.WaitGroup;

const Manifest = @import("Manifest.zig");

var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};

/// 1MB git output
const MAX_TEMPLATE_SIZE = 1024 * 1024;

pub fn fatal(comptime format: []const u8, args: anytype) noreturn {
    std.log.err(format, args);
    process.exit(1);
}

pub fn main() !void {
    const gpa = general_purpose_allocator.allocator();
    defer _ = general_purpose_allocator.deinit();
    var arena_instance = std.heap.ArenaAllocator.init(gpa);
    defer arena_instance.deinit();
    const arena = arena_instance.allocator();

    const args = try process.argsAlloc(arena);

    const command_arg = args[0];
    _ = command_arg;

    if (args.len > 1) {
        const arg1 = args[1];
        if (mem.eql(u8, arg1, "-h") or mem.eql(u8, arg1, "--help")) {
            try showHelp();
            return;
        }
        if (mem.eql(u8, arg1, "-g") or mem.eql(u8, arg1, "--git")) {
            try cmdPkgGit(gpa, args);
            return;
        }
    }

    try cmdPkg(gpa, arena, args);
}

fn showHelp() !void {
    const stdout = io.getStdOut().writer();
    try stdout.writeAll(usage_pkg);
}

pub const usage_pkg =
    \\Usage: pkghash [options]
    \\
    \\Options: 
    \\  -h --help           Print this help and exit.
    \\  -g --git            Use git ls-files
    \\
    \\Sub-options: 
    \\  --allow-directory : calc hash even if no build.zig is present
    \\                      applies in no-git mode only
    \\
    \\Sub-options for --git: 
    \\  --tag=<tag>          : specify git tag to use in template
    \\                         defaults to tag pointing to HEAD
    \\  --template=<file.md> : specify markdown template to render
;

pub fn gitLatestTag(gpa: Allocator, pkg_dir: []const u8) ![]const u8 {
    const result = try std.ChildProcess.exec(.{
        .allocator = gpa,
        .argv = &.{
            "git",
            "-C",
            pkg_dir,
            "tag",
            "--contains=HEAD",
        },
        .cwd = pkg_dir,
        // cwd_dir: ?fs.Dir = null,
        // env_map: ?*const EnvMap = null,
        // max_output_bytes: usize = 50 * 1024,
        // expand_arg0: Arg0Expand = .no_expand,
    });
    defer gpa.free(result.stderr);
    const retcode = switch (result.term) {
        .Exited => |exitcode| exitcode,
        else => return error.GitError,
    };
    if (retcode != 0) return error.GitError;

    return result.stdout;
}

pub fn gitFileList(gpa: Allocator, pkg_dir: []const u8) ![]const u8 {
    const result = try std.ChildProcess.exec(.{
        .allocator = gpa,
        .argv = &.{
            "git",
            "-C",
            pkg_dir,
            "ls-files",
        },
        .cwd = pkg_dir,
        // cwd_dir: ?fs.Dir = null,
        // env_map: ?*const EnvMap = null,
        // max_output_bytes: usize = 50 * 1024,
        // expand_arg0: Arg0Expand = .no_expand,
    });
    defer gpa.free(result.stderr);
    const retcode = switch (result.term) {
        .Exited => |exitcode| exitcode,
        else => return error.GitError,
    };
    if (retcode != 0) return error.GitError;

    return result.stdout;
}

pub fn cmdPkgGit(gpa: Allocator, args: []const []const u8) !void {
    if (args.len == 0) fatal("Expected at least one argument.\n", .{});

    const cwd = std.fs.cwd();
    const cwd_absolute_path = try cwd.realpathAlloc(gpa, ".");
    defer gpa.free(cwd_absolute_path);

    var do_render_template = false;
    var template_filn: ?[]const u8 = null;
    var git_tag: ?[]const u8 = null;

    const arg_tag = "--tag=";
    const arg_template = "--template=";
    for (args) |arg| {
        if (std.mem.startsWith(u8, arg, arg_tag)) {
            if (arg.len > arg_tag.len) {
                git_tag = try gpa.dupe(u8, arg[arg_tag.len..]);
                do_render_template = true;
            } else {
                std.debug.print(
                    \\Error: --tag=... requires a tag after the =
                , .{});
                try showHelp();
                return;
            }
        }

        if (std.mem.startsWith(u8, arg, arg_template)) {
            if (arg.len > arg_template.len) {
                template_filn = arg[arg_template.len..];
                do_render_template = true;
            } else {
                std.debug.print(
                    \\Error: --template=... requires a filename after the =
                , .{});
                try showHelp();
                return;
            }
        }
    }
    if (do_render_template) {
        if (template_filn == null) {
            std.debug.print(
                \\Error: if --tag=... is provided, --template= must be provided, too!
                \\Use -h for help
            , .{});
            try showHelp();
            return;
        }
        if (git_tag == null) {
            // try to get the latest tag
            if (gitLatestTag(gpa, cwd_absolute_path)) |tag_slice| {
                // strip \n
                defer gpa.free(tag_slice);
                if (tag_slice.len > 1) {
                    git_tag = try gpa.dupe(u8, tag_slice[0 .. tag_slice.len - 1]);
                } else {
                    std.debug.print(
                        \\Error: could not deduce git tag! Provide --tag=
                        \\Use -h for help
                    , .{});
                    try showHelp();
                    return;
                }
            } else |_| {
                std.debug.print(
                    \\Error: if --template=... is provided, --tag= must be provided, too!
                    \\Use -h for help
                , .{});
                try showHelp();
                return;
            }
        }
    }

    errdefer {
        if (git_tag) |g| {
            gpa.free(g);
        }
    }

    const hash = blk: {
        const result = try gitFileList(gpa, cwd_absolute_path);
        defer gpa.free(result);

        var thread_pool: ThreadPool = undefined;
        try thread_pool.init(.{ .allocator = gpa });
        defer thread_pool.deinit();

        break :blk try computePackageHashForFileList(
            &thread_pool,
            cwd,
            result,
        );
    };

    const std_out = std.io.getStdOut();
    const digest = Manifest.hexDigest(hash);
    const digest_slice = digest[0..];
    if (!do_render_template) {
        try std_out.writeAll(digest_slice);
        try std_out.writeAll("\n");
    } else {
        try renderTemplate(gpa, git_tag.?, template_filn.?, digest_slice);
    }
    if (git_tag) |g| {
        gpa.free(g);
    }
}

fn renderTemplate(gpa: std.mem.Allocator, tag: []const u8, template: []const u8, hash: []const u8) !void {
    const contents = try std.fs.cwd().readFileAlloc(gpa, template, MAX_TEMPLATE_SIZE);
    defer gpa.free(contents);
    const s1 = try std.mem.replaceOwned(u8, gpa, contents, "{tag}", tag);
    defer gpa.free(s1);
    const s2 = try std.mem.replaceOwned(u8, gpa, s1, "{hash}", hash);
    defer gpa.free(s2);

    try std.io.getStdOut().writer().writeAll(s2);
}

pub fn cmdPkg(gpa: Allocator, arena: Allocator, args: []const []const u8) !void {
    _ = arena;

    const cwd = std.fs.cwd();

    dir_test: {
        if (args.len > 1 and mem.eql(u8, args[1], "--allow-directory")) break :dir_test;
        try if (cwd.access("build.zig", .{})) |_| break :dir_test else |err| switch (err) {
            error.FileNotFound => {},
            else => |e| e,
        };
        try if (cwd.access("build.zig.zon", .{})) |_| break :dir_test else |err| switch (err) {
            error.FileNotFound => {},
            else => |e| e,
        };
        break :dir_test fatal("Could not find either build.zig or build.zig.zon in this directory.\n Use --allow-directory to override this check.\n", .{});
    }

    const hash = blk: {
        const cwd_absolute_path = try cwd.realpathAlloc(gpa, ".");
        defer gpa.free(cwd_absolute_path);

        // computePackageHash will close the directory after completion
        // std.debug.print("abspath: {s}\n", .{cwd_absolute_path});
        var cwd_copy = try fs.openIterableDirAbsolute(cwd_absolute_path, .{});
        errdefer cwd_copy.dir.close();

        var thread_pool: ThreadPool = undefined;
        try thread_pool.init(.{ .allocator = gpa });
        defer thread_pool.deinit();

        // workaround for missing inclusion/exclusion support -> #14311.
        const excluded_directories: []const []const u8 = &.{
            "zig-out",
            "zig-cache",
            ".git",
        };
        break :blk try computePackageHashExcludingDirectories(
            &thread_pool,
            .{ .dir = cwd_copy.dir },
            excluded_directories,
        );
    };

    const std_out = std.io.getStdOut();
    const digest = Manifest.hexDigest(hash);
    try std_out.writeAll(digest[0..]);
    try std_out.writeAll("\n");
}

/// Make a file system path identical independently of operating system path inconsistencies.
/// This converts backslashes into forward slashes.
fn normalizePath(arena: Allocator, fs_path: []const u8) ![]const u8 {
    const canonical_sep = '/';

    if (fs.path.sep == canonical_sep)
        return fs_path;

    const normalized = try arena.dupe(u8, fs_path);
    for (normalized) |*byte| {
        switch (byte.*) {
            fs.path.sep => byte.* = canonical_sep,
            else => continue,
        }
    }
    return normalized;
}

const HashedFile = struct {
    fs_path: []const u8,
    normalized_path: []const u8,
    hash: [Manifest.Hash.digest_length]u8,
    failure: Error!void,

    const Error = fs.File.OpenError || fs.File.ReadError || fs.File.StatError;

    fn lessThan(context: void, lhs: *const HashedFile, rhs: *const HashedFile) bool {
        _ = context;
        return mem.lessThan(u8, lhs.normalized_path, rhs.normalized_path);
    }
};

fn workerHashFile(dir: fs.Dir, hashed_file: *HashedFile, wg: *WaitGroup) void {
    defer wg.finish();
    hashed_file.failure = hashFileFallible(dir, hashed_file);
}

fn hashFileFallible(dir: fs.Dir, hashed_file: *HashedFile) HashedFile.Error!void {
    var buf: [8000]u8 = undefined;
    var file = try dir.openFile(hashed_file.fs_path, .{});
    defer file.close();
    var hasher = Manifest.Hash.init(.{});
    hasher.update(hashed_file.normalized_path);
    hasher.update(&.{ 0, @intFromBool(try isExecutable(file)) });
    while (true) {
        const bytes_read = try file.read(&buf);
        if (bytes_read == 0) break;
        hasher.update(buf[0..bytes_read]);
    }
    hasher.final(&hashed_file.hash);
}

fn isExecutable(file: fs.File) !bool {
    _ = file;
    // hack: in order to mimic current zig's tar extraction, we set everything to
    // NOT EXECUTABLE
    // const stat = try file.stat();
    // return (stat.mode & std.os.S.IXUSR) != 0;
    return false;
}

pub fn computePackageHashExcludingDirectories(
    thread_pool: *ThreadPool,
    pkg_dir: fs.IterableDir,
    excluded_directories: []const []const u8,
) ![Manifest.Hash.digest_length]u8 {
    const gpa = thread_pool.allocator;

    // We'll use an arena allocator for the path name strings since they all
    // need to be in memory for sorting.
    var arena_instance = std.heap.ArenaAllocator.init(gpa);
    defer arena_instance.deinit();
    const arena = arena_instance.allocator();

    // Collect all files, recursively, then sort.
    var all_files = std.ArrayList(*HashedFile).init(gpa);
    defer all_files.deinit();

    var walker = try pkg_dir.walk(gpa);
    defer walker.deinit();

    {
        // The final hash will be a hash of each file hashed independently. This
        // allows hashing in parallel.
        var wait_group: WaitGroup = .{};
        defer wait_group.wait();

        loop: while (try walker.next()) |entry| {
            switch (entry.kind) {
                .directory => {
                    for (excluded_directories) |dir_name| {
                        if (mem.eql(u8, entry.basename, dir_name)) {
                            var item = walker.stack.pop();
                            if (walker.stack.items.len != 0) {
                                item.iter.dir.close();
                            }
                            continue :loop;
                        }
                    }
                    continue :loop;
                },
                .file => {},
                else => return error.IllegalFileTypeInPackage,
            }
            const hashed_file = try arena.create(HashedFile);
            const fs_path = try arena.dupe(u8, entry.path);
            hashed_file.* = .{
                .fs_path = fs_path,
                .normalized_path = try normalizePath(arena, fs_path),
                .hash = undefined, // to be populated by the worker
                .failure = undefined, // to be populated by the worker
            };
            wait_group.start();
            try thread_pool.spawn(workerHashFile, .{ pkg_dir.dir, hashed_file, &wait_group });

            try all_files.append(hashed_file);
        }
    }

    std.mem.sort(*HashedFile, all_files.items, {}, HashedFile.lessThan);

    var hasher = Manifest.Hash.init(.{});
    var any_failures = false;
    for (all_files.items) |hashed_file| {
        hashed_file.failure catch |err| {
            any_failures = true;
            std.log.err("unable to hash '{s}': {s}", .{ hashed_file.fs_path, @errorName(err) });
        };
        // std.debug.print("{s} : {s}\n", .{ hashed_file.normalized_path, Manifest.hexDigest(hashed_file.hash) });
        hasher.update(&hashed_file.hash);
    }
    if (any_failures) return error.PackageHashUnavailable;
    return hasher.finalResult();
}

pub fn computePackageHashForFileList(
    thread_pool: *ThreadPool,
    pkg_dir: fs.Dir,
    file_list: []const u8,
) ![Manifest.Hash.digest_length]u8 {
    const gpa = thread_pool.allocator;

    // We'll use an arena allocator for the path name strings since they all
    // need to be in memory for sorting.
    var arena_instance = std.heap.ArenaAllocator.init(gpa);
    defer arena_instance.deinit();
    const arena = arena_instance.allocator();

    // Collect all files, recursively, then sort.
    var all_files = std.ArrayList(*HashedFile).init(gpa);
    defer all_files.deinit();
    {
        // The final hash will be a hash of each file hashed independently. This
        // allows hashing in parallel.
        var wait_group: WaitGroup = .{};
        defer wait_group.wait();

        var it = std.mem.split(u8, file_list, "\n");

        while (it.next()) |entry| {
            if (entry.len > 0) {
                const hashed_file = try arena.create(HashedFile);
                const fs_path = try arena.dupe(u8, entry);
                hashed_file.* = .{
                    .fs_path = fs_path,
                    .normalized_path = try normalizePath(arena, fs_path),
                    .hash = undefined, // to be populated by the worker
                    .failure = undefined, // to be populated by the worker
                };
                wait_group.start();
                try thread_pool.spawn(workerHashFile, .{ pkg_dir, hashed_file, &wait_group });

                try all_files.append(hashed_file);
            }
        }
    }

    std.mem.sort(*HashedFile, all_files.items, {}, HashedFile.lessThan);

    var hasher = Manifest.Hash.init(.{});
    var any_failures = false;
    for (all_files.items) |hashed_file| {
        hashed_file.failure catch |err| {
            any_failures = true;
            std.log.err("unable to hash '{s}': {s}", .{ hashed_file.fs_path, @errorName(err) });
        };
        // std.debug.print("{s} : {s}\n", .{ hashed_file.normalized_path, Manifest.hexDigest(hashed_file.hash) });
        hasher.update(&hashed_file.hash);
    }
    if (any_failures) return error.PackageHashUnavailable;
    return hasher.finalResult();
}
