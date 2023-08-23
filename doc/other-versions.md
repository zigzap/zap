# Alternatives to released versions


## Using a tagged version

Go to [to the tags page](https://github.com/zigzap/zap/tags) to view all
available tagged versions of zap. From there, right click on the `tar.gz` link
to copy the URL to put into your `build.zig.zon`.

After changing the `.url` field, you will get an error like this at the next
attempt to `zig build`:

```
.../build.zig.zon:8:21: error: hash mismatch:
expected: 12205fd0b60720fb2a40d82118ee75c15cb5589bb9faf901c8a39a93551dd6253049,
found: 1220f4ea8be4a85716ae1362d34c077dca10f10d1baf9196fc890e658c56f78b7424
.hash = "12205fd0b60720fb2a40d82118ee75c15cb5589bb9faf901c8a39a93551dd6253049",
^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

**Note:** If you don't get this error, clean your global zig cache: `rm -fr
~/.cache/zig`. This shouldn't happen with current zig master anymore.

With the new URL, the old hash in the `build.zig.zon` is no longer valid. You
need to take the hash value displayed after `found: ` in the error message as
the `.hash` value in `build.zig.zon`.


## Using an arbitrary (last) commit

Use the same workflow as above for tags, excpept for the URL, use this schema:

```zig
.url = "https://github.com/zigzap/zap/archive/[COMMIT-HASH].tar.gz",
```

Replace `[COMMIT-HASH]` with the full commit hash as provided, e.g. by `git
log`.


