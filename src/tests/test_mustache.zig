const std = @import("std");
const zap = @import("zap");

const User = struct {
    name: []const u8,
    id: isize,
};

const data = .{
    .users = [_]User{
        .{
            .name = "Rene",
            .id = 1,
        },
        .{
            .name = "Caro",
            .id = 6,
        },
    },
    .nested = .{
        .item = "nesting works",
    },
};

test "MustacheData" {
    const template = "{{=<< >>=}}* Users:\n<<#users>><<id>>. <<& name>> (<<name>>)\n<</users>>\nNested: <<& nested.item >>.";
    const p = try zap.MustacheData(template);
    defer zap.MustacheFree(p);

    const ret = zap.MustacheBuild(p, data);
    defer ret.deinit();

    try std.testing.expectEqualSlices(u8, "* Users:\n1. Rene (Rene)\n6. Caro (Caro)\nNested: nesting works.", ret.str().?);
}

test "MustacheLoad" {
    const p = try zap.MustacheLoad("./src/tests/testtemplate.html");
    defer zap.MustacheFree(p);

    const ret = zap.MustacheBuild(p, data);
    defer ret.deinit();

    try std.testing.expectEqualSlices(u8, "* Users:\n1. Rene (Rene)\n6. Caro (Caro)\nNested: nesting works.\n", ret.str().?);
}
