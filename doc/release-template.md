# ZAP Release {tag}

To use in your own projects, put this dependency into your `build.zig.zon`:

```zig
        // zap {tag}
        .zap = .{
            .url = "https://github.com/zigzap/zap/archive/refs/tags/{tag}.tar.gz",
            .hash = "{hash}",
        }
```

Here is a complete `build.zig.zon` example:

```zig
.{
    .name = "My example project",
    .version = "0.0.1",

    .dependencies = .{
        // zap {tag}
        .zap = .{
            .url = "https://github.com/zigzap/zap/archive/refs/tags/{tag}.tar.gz",
            .hash = "{hash}",
        },
        .paths = .{
            "",
        },
    }
}

```

Then, in your `build.zig`'s `build` function, add the following before
`b.installArtifact(exe)`:

```zig 
    const zap = b.dependency("zap", .{
        .target = target,
        .optimize = optimize,
        .openssl = false, // set to true to enable TLS support
    });
    exe.root_module.addImport("zap", zap.module("zap"));
```
