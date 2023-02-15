// supporting code to make using facilio's mustache stuff
// (see http://facil.io/0.7.x/fiobj_mustache)
// easier / possible / more zig-like

const C = @cImport({
    @cInclude("mustache_parser.h");
    @cInclude("fiobj_mustache.h");
});

pub const MustacheLoadArgs = extern struct {
    filename: [*c]const u8,
    filename_len: usize,
    data: [*c]const u8,
    data_len: usize,
    err: [*c]C.mustache_error_en,
};

// pub const struct_mustache_s = opaque {};
// pub const mustache_s = struct_mustache_s;
pub const Mustache = C.mustache_s;

pub const MustacheStatus = enum(c_int) {};

pub const MustacheError = error{
    MUSTACHE_ERR_TOO_DEEP,
    MUSTACHE_ERR_CLOSURE_MISMATCH,
    MUSTACHE_ERR_FILE_NOT_FOUND,
    MUSTACHE_ERR_FILE_TOO_BIG,
    MUSTACHE_ERR_FILE_NAME_TOO_LONG,
    MUSTACHE_ERR_FILE_NAME_TOO_SHORT,
    MUSTACHE_ERR_EMPTY_TEMPLATE,
    MUSTACHE_ERR_DELIMITER_TOO_LONG,
    MUSTACHE_ERR_NAME_TOO_LONG,
    MUSTACHE_ERR_UNKNOWN,
    MUSTACHE_ERR_USER_ERROR,
};

// pub extern fn fiobj_mustache_load(filename: fio_str_info_s) ?*mustache_s;

// implement these: fiobj_mustache.c
// pub extern fn fiobj_mustache_new(args: mustache_load_args_s) ?*mustache_s;
// pub extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;
// pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
// pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;

pub fn MustacheNew(data: []const u8) MustacheError!*Mustache {
    var err: C.mustache_error_en = undefined;
    var args: MustacheLoadArgs = .{
        .filename = null,
        .filename_len = 0,
        .data = data,
        .data_len = data.len,
        .err = &err,
    };
    var ret = C.fiobj_mustache_new(args);
    switch (err) {
        0 => return ret,
        1 => return MustacheError.MUSTACHE_ERR_TOO_DEEP,
        2 => return MustacheError.MUSTACHE_ERR_CLOSURE_MISMATCH,
        3 => return MustacheError.MUSTACHE_ERR_FILE_NOT_FOUND,
        4 => return MustacheError.MUSTACHE_ERR_FILE_TOO_BIG,
        5 => return MustacheError.MUSTACHE_ERR_FILE_NAME_TOO_LONG,
        6 => return MustacheError.MUSTACHE_ERR_FILE_NAME_TOO_SHORT,
        7 => return MustacheError.MUSTACHE_ERR_EMPTY_TEMPLATE,
        8 => return MustacheError.MUSTACHE_ERR_DELIMITER_TOO_LONG,
        9 => return MustacheError.MUSTACHE_ERR_NAME_TOO_LONG,
        10 => return MustacheError.MUSTACHE_ERR_UNKNOWN,
        11 => return MustacheError.MUSTACHE_ERR_USER_ERROR,
        else => MustacheError.MustacheError.MUSTACHE_ERR_UNKNOWN,
    }
}

test "MustacheNew" {
    const template = "{{=<< >>=}}* Users:\r\n<<#users>><<id>>. <<& name>> (<<name>>)\r\n<</users>>\r\nNested: <<& nested.item >>.";
    _ = MustacheNew(template);
}
