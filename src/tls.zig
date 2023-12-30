const fio = @import("fio.zig");

/// Server-Side TLS function wrapper
const Tls = @This();

fio_tls: ?*anyopaque = null,

/// TLS settings used in init() and addCertificate()
/// If all values are NULL, a TLS object wll be created without a
/// certificate. This could be used for clients together with Tls.trust().
pub const TlsSettings = struct {
    /// If a server name is provided, then NULL values _can_ be used to create an anonymous (unverified)
    /// context / settings object.
    server_name: ?[*:0]const u8 = null,
    public_certificate_file: ?[*:0]const u8 = null,
    private_key_file: ?[*:0]const u8 = null,
    /// The private_key_password can be NULL if the private key PEM file isn't password protected.
    private_key_password: ?[*:0]const u8 = null,
};

/// Creates a new SSL/TLS context / settings object with a default certificate (if any).
/// If a server name is provided, then NULL values can be used to create an anonymous (unverified)
/// context / settings object. If all values are NULL, a TLS object will be created without a
/// certificate. This could be used for clients together with Tls.trust().
/// The private_key_password can be NULL if the private key PEM file isn't password protected.
pub fn init(settings: TlsSettings) !Tls {
    const ret = fio.fio_tls_new(
        settings.server_name,
        settings.public_certificate_file,
        settings.private_key_file,
        settings.private_key_password,
    );
    if (ret == null) return error.FileNotFound;
    return Tls{ .fio_tls = ret };
}

/// Destroys the SSL/TLS context / settings object and frees any related resources / memory.
pub fn deinit(tls: *const Tls) void {
    fio.fio_tls_destroy(tls.fio_tls);
}

// pub fn incRefCount(tls: *Tls) !void {
//     if (tls.fio_tls == null) {
//         return error.Uninitialized;
//     }
//     fio.fio_tls_dup(tls.fio_tls);
// }

/// Adds a certificate a new SSL/TLS context / settings object (SNI support).
/// The private_key_password can be NULL if the private key PEM file isn't password protected.
pub fn addCertificate(tls: *Tls, settings: TlsSettings) !void {
    if (tls.fio_tls == null) {
        return error.Uninitialized;
    }

    const ret = fio.fio_tls_cert_add(
        tls.fio_tls,
        settings.server_name,
        settings.public_certificate_file,
        settings.private_key_file,
        settings.private_key_password,
    );

    if (ret != 0) return error.FileNotFound;
    return;
}

/// Adds a certificate to the "trust" list, which automatically adds a peer verification requirement.
/// Note: when the fio_tls_s object is used for server connections, this will limit connections to
/// clients that connect using a trusted certificate.
pub fn trust(tls: *Tls, public_cert_file: [*:0]const u8) !void {
    if (tls.fio_tls == null) {
        return error.Uninitialized;
    }

    const ret = fio.fio_tls_trust(tls.fio_tls, public_cert_file);
    if (ret != 0) return error.FileNotFound;
    return;
}
