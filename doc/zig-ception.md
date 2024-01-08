# ZIG-CEPTION!

In ZAP, we have great zig-ception moment in the [middleware
example](../examples/middleware/middleware.zig). But first we need to introduce
one key function of `zap.Middleware`: **combining structs at comptime!**

## Combining structs at runtime

Here is how it is used in user-code:

```zig
// create a combined context struct
const Context = struct {
    user: ?UserMiddleWare.User = null,
    session: ?SessionMiddleWare.Session = null,
};
```

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
const Context = struct {
    user: ?UserMiddleWare.User = null,
    session: ?SessionMiddleWare.Session = null,
};

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
