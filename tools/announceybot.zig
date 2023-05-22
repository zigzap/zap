const std = @import("std");
const PkgHash = @import("pkghash.zig");
const Manifest = @import("Manifest.zig");

const README_PATH = "README.md";
const README_MAX_SIZE = 25 * 1024;
const README_UPDATE_TEMPLATE = @embedFile("./announceybot/release-dep-update-template.md");
const RELEASE_NOTES_TEMPLATE = @embedFile("./announceybot/release-note-template.md");
const RELEASE_ANNOUNCEMENT_TEMPLATE = @embedFile("./announceybot/release-announcement-template.md");
const REPLACE_BEGIN_MARKER = "<!-- INSERT_DEP_BEGIN -->";
const REPLACE_END_MARKER = "<!-- INSERT_DEP_END -->";

fn usage() void {
    const message =
        \\
        \\Usage: announceybot <command> [<tagname>]
        \\
        \\Commands:
        \\    help         : prints this help, no tag required
        \\
        \\    announce     : announce release in #announce discord channel
        \\                   expects the discord webhook URL in the env var
        \\                   named `WEBHOOK_URL`
        \\
        \\    release-notes: print release notes for the given git tag
        \\
        \\    update-readme: modify the README.md to the latest build.zig.zon 
        \\                   instructions
    ;
    std.debug.print("{s}", .{message});
    std.os.exit(1);
}

var general_purpose_allocator = std.heap.GeneralPurposeAllocator(.{}){};

pub fn main() !void {
    const gpa = general_purpose_allocator.allocator();
    defer _ = general_purpose_allocator.deinit();

    var arena_instance = std.heap.ArenaAllocator.init(gpa);
    defer arena_instance.deinit();
    const arena = arena_instance.allocator();

    const args = try std.process.argsAlloc(arena);
    if (args.len < 3) {
        // includes help command :-)
        return usage();
    }

    const command = args[1];
    const tagname = args[2];

    if (std.mem.eql(u8, command, "help")) {
        return usage();
    }

    if (std.mem.eql(u8, command, "announce")) {
        return command_announce(gpa, tagname);
    }

    if (std.mem.eql(u8, command, "release-notes")) {
        return command_releasenotes(gpa, tagname);
    }

    if (std.mem.eql(u8, command, "update-readme")) {
        return command_update_readme(gpa, tagname);
    }

    // undocumented commands
    if (std.mem.eql(u8, command, "tag-annotation")) {
        const annotation = try get_tag_annotation(gpa, tagname);
        defer gpa.free(annotation);
        std.debug.print("{s}\n", .{annotation});
        return;
    }

    // default: command not found
    return usage();
}

/// returns the tag's annotation you own and must free
fn get_tag_annotation(allocator: std.mem.Allocator, tagname: []const u8) ![]const u8 {
    const args = [_][]const u8{
        "git",
        "tag",
        "-l",
        "--format=%(contents)",
        tagname,
    };

    const result = try std.ChildProcess.exec(.{
        .allocator = allocator,
        .argv = &args,
    });
    const return_string = switch (result.term) {
        .Exited => |code| if (code == 0) result.stdout else result.stderr,
        else => result.stderr,
    };

    defer {
        allocator.free(result.stdout);
        allocator.free(result.stderr);
    }
    return try allocator.dupe(u8, return_string);
}

/// you need to have checked out the git tag!
fn getPkgHash(allocator: std.mem.Allocator) ![]const u8 {
    const cwd = std.fs.cwd();
    const cwd_absolute_path = try cwd.realpathAlloc(allocator, ".");
    defer allocator.free(cwd_absolute_path);
    const hash = blk: {
        const result = try PkgHash.gitFileList(allocator, cwd_absolute_path);
        defer allocator.free(result);

        var thread_pool: std.Thread.Pool = undefined;
        try thread_pool.init(.{ .allocator = allocator });
        defer thread_pool.deinit();

        break :blk try PkgHash.computePackageHashForFileList(
            &thread_pool,
            cwd,
            result,
        );
    };

    const digest = Manifest.hexDigest(hash);
    return try allocator.dupe(u8, digest[0..]);
}

const RenderParams = struct {
    tag: ?[]const u8 = null,
    hash: ?[]const u8 = null,
    annotation: ?[]const u8 = null,
};

fn renderTemplate(allocator: std.mem.Allocator, template: []const u8, substitutes: RenderParams) ![]const u8 {
    const the_tag = substitutes.tag orelse "";
    const the_hash = substitutes.hash orelse "";
    const the_anno = substitutes.annotation orelse "";

    const s1 = try std.mem.replaceOwned(u8, allocator, template, "{tag}", the_tag);
    defer allocator.free(s1);
    const s2 = try std.mem.replaceOwned(u8, allocator, s1, "{hash}", the_hash);
    defer allocator.free(s2);
    return try std.mem.replaceOwned(u8, allocator, s2, "{annotation}", the_anno);
}

fn sendToDiscordPart(allocator: std.mem.Allocator, url: []const u8, message_json: []const u8) !void {
    // url
    const uri = try std.Uri.parse(url);

    // http headers
    var h = std.http.Headers{ .allocator = allocator };
    defer h.deinit();
    try h.append("accept", "*/*");
    try h.append("Content-Type", "application/json");

    // client
    var http_client: std.http.Client = .{ .allocator = allocator };
    defer http_client.deinit();

    // request
    var req = try http_client.request(.POST, uri, h, .{});
    defer req.deinit();

    req.transfer_encoding = .chunked;

    // connect, send request
    try req.start();

    // send POST payload
    try req.writer().writeAll(message_json);
    try req.finish();

    // wait for response
    try req.wait();
    var buffer: [1024]u8 = undefined;
    _ = try req.readAll(&buffer);
}

fn sendToDiscord(allocator: std.mem.Allocator, url: []const u8, message: []const u8) !void {
    // json payload
    // max size: 100kB
    var buf: []u8 = try allocator.alloc(u8, 100 * 1024);
    defer allocator.free(buf);
    var fba = std.heap.FixedBufferAllocator.init(buf);
    var string = std.ArrayList(u8).init(fba.allocator());
    try std.json.stringify(.{ .content = message }, .{}, string.writer());

    // We need to split shit into max 2000 characters
    if (string.items.len < 1999) {
        defer string.deinit();
        try sendToDiscordPart(allocator, url, string.items);
        return;
    }

    // we don't use it anymore
    string.deinit();
    fba.reset();

    // we can re-use the buf now

    // we need to split
    // we split the string at 1500 chars max. This should leave plenty
    // of room for the encoded message being < 2000 chars
    const SPLIT_THRESHOLD = 1500;

    const Desc = struct {
        from: usize,
        to: usize,
    };
    var chunks = std.ArrayList(Desc).init(allocator);
    defer chunks.deinit();
    var i: usize = 0;
    var chunk_i: usize = 0;
    var last_newline_index: usize = 0;
    var last_from: usize = 0;
    var in_code_block: bool = false;

    std.debug.print("Needing to split message of size {}.\n", .{message.len});
    while (true) {
        if (chunk_i > SPLIT_THRESHOLD) {
            // start a new chunk
            // we assume, there was a newline in 1990 bytes
            // try chunks.append(message[last_newline_index..i]);
            try chunks.append(.{ .from = last_from, .to = last_newline_index });
            chunk_i = 0;
            last_from = last_newline_index + 1;
            i = last_from;
            if (i >= message.len) {
                break;
            }
            continue;
        }
        if (message[i] == '\n') {
            // we won't use any newline, only ones outside of code blocks
            var next_line_is_code_block_marker: bool = false;
            if (i + 3 < message.len) {
                if (std.mem.eql(u8, message[i + 1 .. i + 4], "```")) {
                    next_line_is_code_block_marker = true;
                    in_code_block = !in_code_block;
                    if (in_code_block) {
                        // we're going to be in a code block
                        // so we can keep the newline that's the last
                        // newline before the code block
                        last_newline_index = i;
                        i += 1;
                        chunk_i += 1;
                        continue;
                    } else {
                        // next line is a code block marker that is
                        // ending the current one, so we can take this
                        // one as the first one that's ok again
                        last_newline_index = i;
                        i += 1;
                        chunk_i += 1;
                        continue;
                    }
                }
            }

            if (in_code_block and next_line_is_code_block_marker) {
                in_code_block = false;
                i += 1;
                chunk_i += 1;
                continue;
            }

            if (in_code_block) {
                i += 1;
                chunk_i += 1;
                continue;
            }

            // we remember everything outside a code block
            last_newline_index = i;
        }

        i += 1;
        chunk_i += 1;

        if (i >= message.len) {
            // push last part
            // try chunks.append(message[last_newline_index..i]);
            try chunks.append(.{ .from = last_from, .to = i });
            break;
        }
    }

    // std.debug.print("Message split into {} parts:\n", .{chunks.items.len});
    //
    // var it: usize = 0;
    // while (it < chunks.items.len) {
    //     std.debug.print("PART {}: {any}\n", .{ it, chunks.items[it] });
    //     it += 1;
    // }
    //
    // it = 0;
    // while (it < chunks.items.len) {
    //     const desc = chunks.items[it];
    //     std.debug.print("PART {}: {s}\n", .{ it, message[desc.from..desc.to] });
    //     it += 1;
    // }

    var it: usize = 0;
    while (it < chunks.items.len) {
        const desc = chunks.items[it];
        const part = message[desc.from..desc.to];
        fba.reset();
        var part_string = std.ArrayList(u8).init(fba.allocator());
        defer part_string.deinit();
        try std.json.stringify(.{ .content = part }, .{}, part_string.writer());
        std.debug.print("SENDING PART {} / {}: ... ", .{ it, chunks.items.len });
        try sendToDiscordPart(allocator, url, part_string.items);
        std.debug.print("done!\n", .{});
        it += 1;
    }
}

fn command_announce(allocator: std.mem.Allocator, tag: []const u8) !void {
    const annotation = try get_tag_annotation(allocator, tag);
    defer allocator.free(annotation);
    const hash = try getPkgHash(allocator);
    defer allocator.free(hash);

    const announcement = try renderTemplate(allocator, RELEASE_ANNOUNCEMENT_TEMPLATE, .{
        .tag = tag,
        .hash = hash,
        .annotation = annotation,
    });

    // std.debug.print("{s}\n", .{announcement});
    defer allocator.free(announcement);
    const url = try std.process.getEnvVarOwned(allocator, "WEBHOOK_URL");
    defer allocator.free(url);
    sendToDiscord(allocator, url, announcement) catch |err| {
        std.debug.print("HTTP ERROR: {any}\n", .{err});
        std.os.exit(1);
    };
}

fn command_releasenotes(allocator: std.mem.Allocator, tag: []const u8) !void {
    const annotation = try get_tag_annotation(allocator, tag);
    defer allocator.free(annotation);
    const hash = try getPkgHash(allocator);
    defer allocator.free(hash);

    const release_notes = try renderTemplate(allocator, RELEASE_NOTES_TEMPLATE, .{
        .tag = tag,
        .hash = hash,
        .annotation = annotation,
    });
    defer allocator.free(release_notes);
    try std.io.getStdOut().writeAll(release_notes);
}
fn command_update_readme(allocator: std.mem.Allocator, tag: []const u8) !void {
    const annotation = try get_tag_annotation(allocator, tag);
    defer allocator.free(annotation);
    const hash = try getPkgHash(allocator);
    defer allocator.free(hash);

    const update_part = try renderTemplate(allocator, README_UPDATE_TEMPLATE, .{
        .tag = tag,
        .hash = hash,
        .annotation = annotation,
    });
    defer allocator.free(update_part);

    // read the readme
    const readme = try std.fs.cwd().readFileAlloc(allocator, README_PATH, README_MAX_SIZE);
    defer allocator.free(readme);

    var output_file = try std.fs.cwd().createFile(README_PATH, .{});
    var writer = output_file.writer();
    defer output_file.close();

    // var writer = std.io.getStdOut().writer();

    // iterate over lines
    var in_replace_block: bool = false;
    var line_it = std.mem.split(u8, readme, "\n");
    while (line_it.next()) |line| {
        if (in_replace_block) {
            if (std.mem.startsWith(u8, line, REPLACE_END_MARKER)) {
                in_replace_block = false;
            }
            continue;
        }
        if (std.mem.startsWith(u8, line, REPLACE_BEGIN_MARKER)) {
            _ = try writer.write(REPLACE_BEGIN_MARKER);
            _ = try writer.write("\n");
            _ = try writer.write(update_part);
            _ = try writer.write(REPLACE_END_MARKER);
            _ = try writer.write("\n");
            in_replace_block = true;
            continue;
        }
        // we need to put the \n back in.
        // TODO: change this by using some "search" iterator that just
        // returns indices etc
        var output_line = try std.fmt.allocPrint(allocator, "{s}\n", .{line});
        defer allocator.free(output_line);
        _ = try writer.write(output_line);
    }
}
