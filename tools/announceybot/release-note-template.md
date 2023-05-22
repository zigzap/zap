# ZAP Release {tag}

## Updates

{annotation}

## Using it

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
        }
    }
}

```

Then, in your `build.zig`'s `build` function, add the following before `exe.install()`:

```zig 
    const zap = b.dependency("zap", .{
        .target = target,
        .optimize = optimize,
    });
    exe.addModule("zap", zap.module("zap"));
    exe.linkLibrary(zap.artifact("facil.io"));
```
