const std = @import("std");

/// HTTP Status codes according to `rfc9110`
/// https://datatracker.ietf.org/doc/html/rfc9110#name-status-codes
/// (modified from https://github.com/Luukdegram/apple_pie/blob/master/src/response.zig)
pub const StatusCode = enum(u16) {
    // Information responses
    @"continue" = 100,
    switching_protocols = 101,
    processing = 102, // (WebDAV)
    early_hints = 103,

    // Successful responses
    ok = 200,
    created = 201,
    accepted = 202,
    non_authoritative_information = 203,
    no_content = 204,
    reset_content = 205,
    partial_content = 206,
    multi_status = 207, // (WebDAV)
    already_reported = 208, // (WebDAV)
    im_used = 226, // (HTTP Delta encoding)

    // Redirection messages
    multiple_choices = 300,
    moved_permanently = 301,
    found = 302,
    see_other = 303,
    not_modified = 304,
    use_proxy = 305,
    unused = 306,
    temporary_redirect = 307,
    permanent_redirect = 308,

    // Client error responses
    bad_request = 400,
    unauthorized = 401,
    payment_required = 402,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    not_acceptable = 406,
    proxy_authentication_required = 407,
    request_timeout = 408,
    conflict = 409,
    gone = 410,
    length_required = 411,
    precondition_failed = 412,
    payload_too_large = 413,
    uri_too_long = 414,
    unsupported_media_type = 415,
    range_not_satisfiable = 416,
    expectation_failed = 417,
    im_a_teapot = 418,
    misdirected_request = 421,
    unprocessable_content = 422, // (WebDAV)
    locked = 423, // (WebDAV)
    failed_dependency = 424, // (WebDAV)
    too_early = 425,
    upgrade_required = 426,
    precondition_required = 428,
    too_many_requests = 429,
    request_header_fields_too_large = 431,
    unavailable_for_legal_reasons = 451,

    // Server error responses
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504,
    http_version_not_supported = 505,
    variant_also_negotiates = 506,
    insufficient_storage = 507, // (WebDAV)
    loop_detected = 508, // (WebDAV)
    not_extended = 510,
    network_authentication_required = 511,
    _,

    /// Returns the string value of a `StatusCode`
    /// for example: .ResetContent returns "Returns Content".
    pub fn toString(self: StatusCode) []const u8 {
        return switch (self) {
            .@"continue" => "Continue",
            .switching_protocols => "Switching Protocols",
            .ok => "Ok",
            .created => "Created",
            .accepted => "Accepted",
            .non_authoritative_information => "Non Authoritative Information",
            .no_content => "No Content",
            .reset_content => "Reset Content",
            .partial_content => "Partial Content",
            .multiple_choices => "Multiple Choices",
            .moved_permanently => "Moved Permanently",
            .found => "Found",
            .see_other => "See Other",
            .not_modified => "Not Modified",
            .use_proxy => "Use Proxy",
            .temporary_redirect => "Temporary Redirect",
            .bad_request => "Bad Request",
            .unauthorized => "Unauthorized",
            .payment_required => "Payment Required",
            .forbidden => "Forbidden",
            .not_found => "Not Found",
            .method_not_allowed => "Method Not Allowed",
            .not_acceptable => "Not Acceptable",
            .proxy_authentication_required => "Proxy Authentication Required",
            .request_timeout => "Request Timeout",
            .conflict => "Conflict",
            .gone => "Gone",
            .length_required => "Length Required",
            .precondition_failed => "Precondition Failed",
            .request_entity_too_large => "Request Entity Too Large",
            .request_uri_too_long => "Request-URI Too Long",
            .unsupported_mediatype => "Unsupported Media Type",
            .requested_range_not_satisfiable => "Requested Range Not Satisfiable",
            .teapot => "I'm a Teapot",
            .upgrade_required => "Upgrade Required",
            .request_header_fields_too_large => "Request Header Fields Too Large",
            .expectation_failed => "Expectation Failed",
            .internal_server_error => "Internal Server Error",
            .not_implemented => "Not Implemented",
            .bad_gateway => "Bad Gateway",
            .service_unavailable => "Service Unavailable",
            .gateway_timeout => "Gateway Timeout",
            .http_version_not_supported => "HTTP Version Not Supported",
            _ => "",
        };
    }
};

pub const Method = enum {
    GET,
    HEAD,
    POST,
    PUT,
    DELETE,
    PATCH,
    OPTIONS,
    UNKNOWN,
};

pub fn methodToEnum(method: ?[]const u8) Method {
    {
        if (method) |m| {
            return std.meta.stringToEnum(Method, m) orelse .UNKNOWN;
        }
        return Method.UNKNOWN;
    }
}
