__**New release {tag}!**__

**Updates**

{annotation}

**Using it**

Modify your `build.zig.zon` like this:

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

See the release page: https://github.com/zigzap/zap/releases/{tag} for more information!
