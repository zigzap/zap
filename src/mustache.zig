// supporting code to make using facilio's mustache stuff
// (see http://facil.io/0.7.x/fiobj_mustache)
// easier / possible / more zig-like

// const C = @cImport({
//     @cInclude("mustache_parser.h");
//     @cInclude("fiobj_mustache.h");
// });

const util = @import("util.zig");

pub const FIOBJ = usize;
pub const struct_mustache_s = opaque {};
pub const enum_mustache_error_en = c_uint;
pub const mustache_error_en = enum_mustache_error_en;

pub const mustache_s = struct_mustache_s;
pub extern fn fiobj_mustache_new(args: MustacheLoadArgs) ?*mustache_s;
pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
pub extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;

pub const MustacheLoadArgs = extern struct {
    filename: [*c]const u8,
    filename_len: usize,
    data: [*c]const u8,
    data_len: usize,
    err: [*c]mustache_error_en,
};

pub const Mustache = mustache_s;

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
    // pub fn MustacheNew(data: []const u8) !*Mustache {
    var err: mustache_error_en = undefined;
    var args: MustacheLoadArgs = .{
        .filename = null,
        .filename_len = 0,
        .data = data.ptr,
        .data_len = data.len,
        .err = &err,
    };
    var ret = fiobj_mustache_new(args);
    switch (err) {
        0 => return ret.?,
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
        else => return MustacheError.MUSTACHE_ERR_UNKNOWN,
    }
}

// implement these: fiobj_mustache.c
// pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
// pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;

pub extern fn fiobj_hash_new() FIOBJ;

// this build is slow because it needs to translate to a FIOBJ data
// object FIOBJ_T_HASH
pub fn MustacheBuild(mustache: *Mustache, data: ?FIOBJ) ?[]const u8 {
    _ = data;

    // FIOBJ data = fiobj_hash_new();
    // FIOBJ key = fiobj_str_new("users", 5);
    // FIOBJ ary = fiobj_ary_new2(4);
    // fiobj_hash_set(data, key, ary);
    // fiobj_free(key);
    // for (int i = 0; i < 4; ++i) {
    //   FIOBJ id = fiobj_str_buf(4);
    //   fiobj_str_write_i(id, i);
    //   FIOBJ name = fiobj_str_buf(4);
    //   fiobj_str_write(name, "User ", 5);
    //   fiobj_str_write_i(name, i);
    //   FIOBJ usr = fiobj_hash_new2(2);
    //   key = fiobj_str_new("id", 2);
    //   fiobj_hash_set(usr, key, id);
    //   fiobj_free(key);
    //   key = fiobj_str_new("name", 4);
    //   fiobj_hash_set(usr, key, name);
    //   fiobj_free(key);
    //   fiobj_ary_push(ary, usr);
    // }
    // key = fiobj_str_new("nested", 6);
    // ary = fiobj_hash_new2(2);
    // fiobj_hash_set(data, key, ary);
    // fiobj_free(key);
    // key = fiobj_str_new("item", 4);
    // fiobj_hash_set(ary, key, fiobj_str_new("dot notation success", 20));
    // fiobj_free(key);

    var empty = fiobj_hash_new();
    var ret = fiobj_mustache_build(mustache, empty);
    return util.fio2str(ret);
}

// pub extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;
pub fn MustacheFree(m: ?*Mustache) void {
    fiobj_mustache_free(m);
}
