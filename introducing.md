# Introducing ⚡zap⚡ - blazingly fast backends in zig

Zap is intended to become my [zig](https://ziglang.org) replacement for the kind of REST APIs I used to write in [python](https://python.org) with [Flask](https://flask.palletsprojects.com) and [mongodb](https://www.mongodb.com), etc. It can be considered to be a microframework for web applications.

What I need for that is a blazingly fast, robust HTTP server that I can use with zig. While facil.io supports TLS, I don't care about HTTPS support. In production, I use [nginx](https://www.nginx.com) as a reverse proxy anyway.

Zap wraps and patches [facil.io - the C web application framework](https://facil.io).

At the time of writing, ZAP is only a few days old and aims to be:

- **robust**
- **fast**
- **minimal**

**⚡ZAP⚡ IS SUPER ALPHA**

_Under the hood, everything is super robust and fast. My zig wrappers are fresh, juicy, and alpha._

Here's what works:

- **Super easy build process**: zap's `build.zig` fetches facilio's git sub-module, applies a patch to its logging for microsecond precision, and then builds and optionally runs everything.
  - _tested on Linux and macOS (arm, M1)_
- **[hello](https://github.com/renerocksai/zap/blob/master/examples/hello/hello.zig)**: welcomes you with some static HTML
- **[routes](https://github.com/renerocksai/zap/blob/master/examples/routes/routes.zig)**: a super easy example dispatching on the HTTP path 
- **[serve](https://github.com/renerocksai/zap/blob/master/examples/serve/serve.zig)**: the traditional static web server with optional dynamic request handling
- **[hello_json](https://github.com/renerocksai/zap/blob/master/examples/hello_json/hello_json.zig)**: serves you json dependent on HTTP path
- **[endpoint](https://github.com/renerocksai/zap/blob/master/examples/endpoint/)**: a simple JSON REST API example featuring a `/users` endpoint for PUTting/DELETE-ing/GET-ting/POST-ing and listing users, together with a static HTML and JavaScript frontend to play with.

If you want to take it for a quick spin: 

```shell
$ git clone https://github.com/renerocksai/zap.git 
$ cd zap 
$ zig build run-hello 
$ # open http://localhost:3000 in your browser 
```

See [the README](https://github.com/renerocksai/zap) for how easy it is to get started, how to run the examples, and how to use zap in your own projects.

I'll continue wrapping more of facil.io's functionality and adding stuff to zap to a point where I can use it as the JSON REST API backend for real research projects, serving thousands of concurrent clients. Now that the endpoint example works, ZAP has actually become pretty usable to me.

**Side-note:** It never ceases to amaze me how productive I can be in zig, eventhough I am still considering myself to be a newbie. Sometimes, it's almost like writing python but with all the nice speed and guarantees that zig gives you. Also, the C integration abilities of zig are just phenomenal! I am super excited about zig's future!

Now, on to the guiding principles of Zap.

## robust

A common recommendation for doing web stuff in zig is to write the actual HTTP server in Go, and use zig for the real work. While there is a selection of notable and cool HTTP server implementations written in zig out there, at the time of writing, most of them seem to a) depend on zig's async facilities which are unsupported until ca. April 2023 when async will return to the self-hosted compiler, and b) have not matured to a point where **I** feel safe using them in production. These are just my opionions and they could be totally wrong though.

However, when I conduct my next online research experiment with thousands of concurrent clients, I cannot afford to run into potential maturity-problems of the HTTP server. These projects typically feature a you-get-one-shot process with little room for errors or re-tries.

With zap, if something should go wrong, at least I'd be close enough to the source-code to, hopefully, be able to fix it in production. With that out of the way, I am super confident that facil.io is very mature compared to many of the alternatives. My `wrk` tests also look promising.

I intend to add app-specific performance tests, e.g. stress-testing the endpoint example, to make sure the zap endpoint framework is able to sustain a high load without running into performance or memory problems. That will be interesting.


## ⚡blazingly fast⚡

Claiming to be blazingly fast is the new black. At least, zap doesn't slow you down and if your server performs poorly, it's probably not exactly zap's fault. Zap relies on the [facil.io](https://facil.io) framework and so it can't really claim any performance fame for itself. In this initial implementation of zap, I didn't care about optimizations at all.

But, how fast is it? Being blazingly fast is relative. When compared with a simple GO HTTP server, a simple zig zap HTTP server performed really good on my machine:

- zig zap was nearly 30% faster than GO
- zig zap had over 50% more throughput than GO

I intentionally only tested static HTTP output, as that seemed to be the best common ground of all test subjects to me. The measurements were for just getting a ballpark baseline anyway.

**Update**: I was intrigued comparing to a basic rust HTTP server. Unfortunately, knowing nothing at all about rust, I couldn't find a simple, just-a-few-lines one like in Go and Python right away and hence tried to go for the one in the book [The Rust Programming Language](https://doc.rust-lang.org/book/ch20-00-final-project-a-web-server.html). Wanting it to be of a somewhat fair comparison, I opted for the multi-threaded example. It didn't work out-of-the-book, but I got it to work (essentially, by commenting out all "print" statements) and changed it to not read files but outputting static text just like the other examples. **Maybe someone with rust experience** can have a look at my [wrk/rust/hello](wrk/rust/hello) code and tell me why it is surprisingly 'slow', as I expected it to be faster than or at least on-par with the basic Go example. I'll enable the GitHub discussions for this matter. My suspicion is bad performance of the mutexes.

![table](https://raw.githubusercontent.com/renerocksai/zap/master/wrk_table_summary.png)

![charts](https://raw.githubusercontent.com/renerocksai/zap/master/wrk_charts_summary.png)

So, being somewhere in the ballpark of basic GO performance, zig zap seems to be ... of reasonable performance 😎.

See more details in [blazingly-fast.md](https://github.com/renerocksai/zap/blob/master/blazingly-fast.md).

## minimal 

Zap is minimal by necessity. I only (have time to) add what I need - for serving REST APIs and HTML. The primary use-case are frontends that I wrote that communicate with my APIs. Hence, the focus is more on getting stuff done rather than conforming to every standard there is. Even though facilio is able to support TLS, I don't care about that - at least for now. Also, if you present `404 - File not found` as human-readable HTML to the user, nobody forces you to also set the status code to 404, so it can be OK to spare those nanoseconds. Gotta go fast!

Facilio comes with Mustache parsing, TLS via third-party libs, websockets, redis support, concurrency stuff, Base64 support, logging facilities, pub/sub / cluster messages API, hash algorithm implementations, its own memory allocator, and so forth. It is really an amazing project!

On the lower level, you can use all of the above by working with `zap.C`. I'll zig-wrap what I need for my projects first, before adding more fancy stuff.

Also, there are nice and well-typed zig implementations for some of the above extra functionalities, and zap-wrapping them needs careful consideration. E.g. it might not be worth the extra effort to wrap facil.io's mustache support when there is a good zig alternative already. Performance / out-of-the-box integration might be arguments pro wrapping them in zap.

## wrapping up - zig is WYSIWYG code

I am super excited about both zig and zap's future. I am still impressed by how easy it is to integrate a C codebase into a zig project, then benefiting from and building on top of battle-tested high-performance C code. Additionally, with zig you get C-like performance with almost Python-like comfort. And you can be sure no exception is trying to get you when you least expect it. No hidden allocations, no hidden control-flows, how cool is that? **WYSIWYG code!**

Provided that the incorporated C code is well-written and -tested, WYSIWYG even holds mostly true for combined Zig and C projects.

You can truly build on the soulders of giants here. Mind you, it took me less than a week to arrive at the current state of zap where I am confident that I can already use it to write the one or other REST API with it and, after stress-testing, just move it into production - from merely researching Zig and C web frameworks a few days ago.

Oh, and have I mentioned Zig's built-in build system and testing framework? Those are both super amazing and super convenient. `zig build` is so much more useful than `make` (which I quite like to be honest). And `zig test` is just amazing, too. Zig's physical code layout: which file is located where and how can it be built, imported, tested - it all makes so much sense. Such a coherent, pleasant experience.

Looking forward, I am also tempted to try adding some log-and-replay facilities as a kind of backup for when things go wrong. I wouldn't be confident to attemt such things in C because I'd view them as being too much work; too much could go wrong. But with Zig, I am rather excited about the possibilities that open up and eager to try such things.

For great justice!
