# ZIG-CEPTION!

In ZAP, we have great zig-ception moment in the [middleware
example](../examples/middleware/middleware.zig). But first we need to introduce
one key function of `zap.Middleware`: **combining structs at comptime!**

## Combining structs at runtime

Here is how it is used in user-code:

```zig
// create a combined context struct
const Context = zap.Middleware.MixContexts(.{
    .{ .name = "?user", .type = UserMiddleWare.User },
    .{ .name = "?session", .type = SessionMiddleWare.Session },
});
```

The result of this function call is a struct that has a `user` field of type
`?UserMiddleWare.User`, which is the `User` struct inside of its containing
struct - and a `session` field of type `?SessionMiddleWare.Session`.

So `MixContexts` accepts a **tuple** of structs that each contain a
`name` field and a `type` field. As a hack, we support the `?` in the name to
indicate we want the resulting struct field to be an optional.

A **tuple** means that we can "mix" as many structs as we like. Not just two
like in the example above.

`MixContexts` inspects the passed-in `type` fields and **composes a new struct
type at comptime**! Have a look at its [source code](../src/middleware.zig).
You'll be blown away if this kind of metaprogramming stuff isn't what you do
everyday. I was totally blown away by trying it out and seeing it that it
_actually_ worked.

Why do we create combined structs? Because all our Middleware handler functions
need to receive a per-request context. But each wants their own data: the User
middleware might want to access a User struct, the Session middleware might want
a Session struct, and so on. So, which struct should we use in the prototype of
the "on_request" callback function? We could just use an `anyopaque` pointer.
That would solve the generic function prototype problem. But then everyone
implementing such a handler would need to cast this pointer back into - what?
Into the same type that the caller of the handler used. It gets really messy
when we continue this train of thought.

So, in ZAP, I opted for one Context type for all request handlers. Since ZAP is
a library, it cannot know what your preferred Context struct is. What it should
consist of. Therefore, it lets you combine all the structs your and maybe your
3rd parties's middleware components require - at comptime! And derive the
callback function prototype from that. If you look at the [middleware
example](../examples/middleware/middleware.zig), you'll notice, it's really
smooth to use.

**NOTE:** In your contexts, please also use OPTIONALS. They are set null at
context creation time. And will aid you in not shooting yourself in the foot
when accessing context fields that haven't been initialized - which may happen
when the order of your chain of components isn't perfect yet. 😉

## The zig-ception moment

Have a look at an excerpt of the example:

```zig
// create a combined context struct
const Context = zap.Middleware.MixContexts(.{
    .{ .name = "?user", .type = UserMiddleWare.User },
    .{ .name = "?session", .type = SessionMiddleWare.Session },
});

// we create a Handler type based on our Context
const Handler = zap.Middleware.Handler(Context);

//
// ZIG-CEPTION!!!
//
// Note how amazing zig is:
// - we create the "mixed" context based on the both middleware structs
// - we create the handler based on this context
// - we create the middleware structs based on the handler
//     - which needs the context
//     - which needs the middleware structs
//     - ZIG-CEPTION!

// Example user middleware: puts user info into the context
const UserMiddleWare = struct {
    handler: Handler,

    // .. the UserMiddleWare depends on the handler
    //    which depends on the Context
    //    which depends on this UserMiddleWare struct
    //    ZIG-CEPTION!!!
```

## 🤯

The comments in the code say it all. 

**Isn't ZIG AMAZING?**
