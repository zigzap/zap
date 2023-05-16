# Why request functions must not return errors

One cool feature of zig is its error handling. I won't elaborate on it here, it
suffices to say that I am a big fan of it.

When creating ZAP, I liked having the callback functions only return `void` and
not `!void`.

Now, coming from some other, more mature framework like FLASK, one might be used
to automatic error handling. E.g. in debug mode, a nice stack trace is returned
that points you directly to where the exception happened, plus a stack-trace.

Wouldn't that be handy? Wouldn't that be a good idea to also make ZAP behave
that way?

My argument is: NO. Not unconditionally like this. 

Let me elaborate: First, ZAP the library cannot know what YOU want to do in case
of an error. If some `on_request()` returns an error, ZAP would only be able to
log it or return some error response - and you might never notice it.

By contrast, if ZAP requires you to not return an error, it can ensure that YOU
will have taken care of ANY error that might happen. It's easy to under-estimate
the consequences. So let me illustrate. Say, in your `on_request()` you start
using some other function, maybe some file I/O with the fictional
`std.fs.do_file_magic()` that you're unfamiliar with. It might have some very
specific error behavior under very specific conditions that you do not expect.
So you assume, the conditions of running your code will never really cause it to
fail. But you're wrong. And you might not notice it until your code is deployed
to production.

So, if `on_request()` were able to return an error, you'd be able to just use
`try std.fs.do_file_magic();` in your `on_request()`, expecting it to never fail
anyway. You run your code locally, test it in your browser or your test suite or
whatever, and deem it fine for production. But on your server, the file system
magic has the magic property of failing for every 1000th request where
`do_file_magic()` returns `RandomMagicErrorThatLooksSuperUnlikely`. What's
worse, you don't notice it via mere code inspection in the source easily since
it's a transient error caused by a call to `try do_internal_magic()` deeply
"hidden" somewhere inside of `do_file_magic()`.

If ZAP's standard error behavior would be to just log it, YOU would probably not
notice it unless you'd monitor your logs carefully. If ZAP's standard error
behavior were to return an error response to the client, you'd eventually hear
from a user that your code has a bug in line XXX of file YYY. Which would be a
shame. Because it could have been avoided easily.

You think that if you had known the magical random error was even an option, you
would probably have put something in place to deal with it. Instead, you let
your assumptions guide you, and now you have to do the work anyway, but under
pressure because production is buggy.

For these reasons, I want YOU to handle YOUR errors YOURSELF. YOU know how to
best react to all possible error scenarios. And by not being able to return an
error, ZIG HELPS YOU to find out what all of these error conditions are.

Now that I think I've made my point 😊, you can, of course always do the
following:

```zig
fn on_request_with_errors(r: zap.SimpleHttpRequest) !void {
    // do all the try stuff here
}
```

```zig
// THIS IS WHAT YOU PASS TO THE LISTENER / ENDPONT / ...
fn on_request(r: zap.SimpleHttpRequest) void {
    on_request_with_errors(r) catch |err| {
        // log the error or use:
        return r.returnWithErrorStackTrace(err);
        // note: above returnWithErrorStackTrace() is vaporware at the moment
    };
}
```

To better support the use-case of flask-like error returning, we will "soon":

- provide a nice returnWithError(err: anyerror) function that you can use
  in your `catch |err| return r.returnWithErrorStackTrace(err);` statements.

- it will honor accept-headers: send json stacktrace, html stacktrace, with
  fallback to text stacktrace.

- only in debug builds would it return the stack traces and in release builds it
  would return more generic 50x responses.


