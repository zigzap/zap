# ZAP Release {tag}

## Updates

{annotation}

## Using it

In your zig project folder (where `build.zig` is located), run:

```
zig fetch --save "git+https://github.com/zigzap/zap#{tag}"
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
