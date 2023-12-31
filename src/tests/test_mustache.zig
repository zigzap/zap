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

test "mustacheData" {
    const template = "{{=<< >>=}}* Users:\n<<#users>><<id>>. <<& name>> (<<name>>)\n<</users>>\nNested: <<& nested.item >>.";

    var mustache = try zap.Mustache.fromData(template);
    defer mustache.deinit();

    const ret = mustache.build(data);
    defer ret.deinit();

    try std.testing.expectEqualSlices(u8, "* Users:\n1. Rene (Rene)\n6. Caro (Caro)\nNested: nesting works.", ret.str().?);
}

test "mustacheLoad" {
    var mustache = try zap.Mustache.fromFile("./src/tests/testtemplate.html");
    defer mustache.deinit();

    const ret = mustache.build(data);
    defer ret.deinit();

    try std.testing.expectEqualSlices(u8, "* Users:\n1. Rene (Rene)\n6. Caro (Caro)\nNested: nesting works.\n", ret.str().?);
}
