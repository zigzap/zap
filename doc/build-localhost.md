# Self-hosting release packages until Zig master is fixed

Recently, GitHub started hosting release archives on a dedicated host
codeload.github.com. This is when the problems started. Back then, zig's package
manager was not expecting to be re-directed to a different URL. On top of that,
GitHub changed the redirected-to URLs so they wouldn't end in `.tar.gz` anymore. 

Above issues were fixed but after some progress on `zig.http` related standard
library stuff, a similar error started impacting the package manager: parsing
long TLS responses has the [issue
ziglang/zig#15990](https://github.com/ziglang/zig/issues/15590).

So, here we are. Since this topic has come up often enough now, it deserves its
own doc.

## The workaround: self-hosting on localhost

My workaround is: not using https! The easiest way to do this, is:

- create the tar archive yourself
- start a python http server on the command line
- replace the URL in the build.zig.zon with a http and localhost one.

For simple packages, this is relatively easy. But zap itself has a
`build.zig.zon` that references its `facilio` dependency. For that reason, ZAP's
build.zig.zon also needs to change: to only reference localhost packages.

The consequence of changing build.zig.zon is: zap's package hash changes! -->
Any build.zig.zon that wants to use ZAP needs to change, too.

This is why, for the time being, I am always creating two releases,
a `release-0.0.n` one and `release-0.0.n-localhost` one, for each release.


So, while the TLS bug persists, you have to use the `-localhost` releases. The
procedure is:

- fetch zap's dependency `facilio` from GitHub
- fetch zap's `localhost` release from GitHub
- _(use the localhost URL and package hash in your build.zig)_
- start a local http server
- run zig build
- stop the http server

Here is an example for the `release-0.0.20-localhost` release which is the
current release at the time of writing:

```console
$ # get dependency required by zap
$ wget https://github.com/zigzap/facil.io/archive/refs/tags/zap-0.0.8.tar.gz
$ # get zap itself
$ wget https://github.com/zigzap/zap/archive/refs/tags/release-0.0.20-localhost.tar.gz
$ # start a http server on port 8000
$ python -m http.server 
```

... and use the following in your build.zig.zon:

```zig
        // zap release-0.0.20-localhost
        .zap = .{
            .url = "http://127.0.0.1/release-0.0.20-localhost.tar.gz",
            .hash = "12204c663be7639e98af40ad738780014b18bcf35efbdb4c701aad51c7dec45abf4d",
        }
```

After the first successful zig build, zig will have cached both dependencies,
the direct zap one and the transient facilio one, and you won't need to start an
HTTP server again until you want to update your dependencies.


## Building Release Packages yourself

- In your branch, replace `build.zig.zon` with `build.zig.zon.localhost`
- **make sure everything is committed and your branch is clean.** This is
  essential for calculating the package hash.
- `zig build pkghash` if you haven't already.
- tag your release: `git tag MY_TAG`
    - I recommend putting `localhost` in the tagname
- `./create-archive.sh MY_TAG`

The `create-archive.sh` script will spit out release notes that contain the
hashes, as well as a `MY_TAG.tar.gz`.

You can then host this via python HTTP server and proceed as if you had
downloaded it from github.

If all goes well, your dependend code should be able to use your freshly-built
zap release, depending on it via localhost URL in its `build.zig.zon`.

If not, fix bugs, rinse, and repeat.

You may want to push to your fork and create a GitHub 'localhost' release.

When you're happy with the release, you may consider replacing `build.zig.zon`
with the non-localhost version from the master branch. Commit it, make sure your
worktree is clean, and perform above steps again. This time, using a tag that
doesn't contain `localhost`. You can then push to your fork and create a release
for the future when zig's bug is fixed.


