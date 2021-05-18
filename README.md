# Fn, a programming language

**Fn** is a programming language which I am currently making for myself.

This is a highly idiosyncratic language consisting of ideas I've taken from
other popular languages, plus a bunch of syntactic conveniences and an object
model of my own contrivance. The most prominent influences present in Fn are
Common Lisp and Python, although there are also ideas taken from Clojure,
Haskell, Lua, and Javascript.

## Development Status

Fn is currently in a pre-alpha development stage. I've finished designing the
core language and am mostly done implementing it. The milestones for the alpha
release are these:

- [x] Fully describe Fn evaluation semantics
- [x] Fully implement a dedicated stack VM which executes Fn bytecode
- [x] Implement all built-in data types
- [ ] Complete support for the core language in the bytecode compiler
- [ ] Implement the core standard library (which consists of a few dozen
      essential functions).
- [ ] Specify and implement a system to load namespaces from external files
- [ ] Foreign function interface to allow calling external code
- [ ] User-friendly REPL (using GNU readline if available)
- [ ] Compile-time code generation via macros
- [x] Frontend which processes command line argument in the expected way

That will take us to alpha and to the first proper version number, 0.1.0. After
that, I'll get to work writing some programs in Fn, fixing bugs as I go, and
eventually, I'll write a brief guide to the language.

I won't be accepting pull requests for now, but curious individuals are welcome
to poke around at the source code. The VM is basically complete and there's a
proof-of-concept compiler, so you can build Fn with CMake and run programs in
it, although there's very little built-in functionality. Crucially, the current
(incomplete) compiler is being abandoned to make way for the improved one
mentioned above. You've been warned.

## Compatibility

Fn officially supports x86_64 Linux environments. That's all I'll promise. It's
developed and tested on Arch Linux, although I've successfully built and run it
on macOS a couple of times out of curiosity. I'm trying to make it support any
mostly UNIX-like environment, which means in theory it should build and run on
everything from OpenBSD to macOS to Windows with MinGW.

## Who is this for?

This is mainly just for me. I write a fair amount of code in my day-to-day life,
and I've always been passionately interested in programming language design and
implementation, so I thought it would be cool to have my own programming
language. I'm not really rushing to get anything done, and in fact there have
been many years of iterative redesigns to get to this point. Actually, I'm very
excited about this project, because after all this time, I think I'm finally
starting to hone in on a really tight and elegant language.

From a design standpoint, what I'm going for is a Lisp dialect that's suitable
for low-performance algorithmic programming, system automation, and embedding in
other programs as a scripting language. I've got a lot of bash and Python
scripts that I'd like to eventually replace with Fn.

It would make me really happy if other people used Fn some day. I'm taking code
quality and testing very seriously, so once we get to that first release, it
should be ready to use in a production environment.


# A Taste of What's to Come

TODO: When I feel like it, I'll put a couple of code samples here.
