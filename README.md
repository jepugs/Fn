# fn, a programming language

**fn** is a programming language which I am currently making for myself. Its features will include

- A simple class-less object system inspired by Lua and Javascript, complete with dot syntax
- A module system I kinda stole from Python
- Filesystem and I/O facilities conveniently built into the standard library
- A powerful metaprogramming system via macros
- Many parentheses

At the time of writing, Version 0 of fn is very nearly complete. Version 0 is a minimal prototype of
fn which supports most of the core language features. You can build it and run code, although
`import` is not fully implemented, so you are limited to a single module.

## Building fn

Build fn by running [make](http://www.gnu.org/software/make/) from the source directory. This will
result in the creation of a self-contained executable called fn. You will also need a version of
[g++](https://gcc.gnu.org/) that supports `-std=C++20`.


### Compatibility

Only 64-bit architectures are supported. fn has been tested (if you can call it that) exclusively in
x86_64 Linux environments. It appears to work on macOS too, using the gcc10 from macports.
Currently, the only platform-specific assumption in the code is that `malloc` returns 8-byte aligned
pointers, which is
[guaranteed by glibc](https//www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html).

The way I'm developing fn should make the codebase fairly portable, especially to UNIX-like
environments. All the compiler and VM logic is meant to be platform-agnostic, and future filesystem
and I/O facilities should work in any POSIX environment. That said, I'm mainly a Linux user, and as
such I'm not going to go out of my way to test on other platforms.


## Development plan

fn is a personal project, but I take its quality seriously. That said, it's definitely not ready for
any use other than sating your sense of curiosity.

To aid in the implementation of fn, I've specced out a version of the language which deliberately
omits certain features. This so-called **Version 0** (corresponding to semantic version 0.1.0) is
nearly finished and implemented. I am currently in the process of finalizing the document describing
the spec, after which point I will be able to finish the interpreter.

I'll start putting out a prereleases of version 0.1.0 as soon as the interpreter is done. I'll still
have some work to do on the standard library at this point before a proper 0.1.0 release.

### Version 0 Features

**Version 0** refers in this document to the first release of fn, i.e. 0.1.0. This release will not
support the full fn language as currently planned.

The following features are planned for the version 0 release:

- Full-featured frontend including an interactive repl and a bytecode disassembler
- Full, unit-tested implementation of a bytecode virtual machine including a mark-and-sweep garbage
  collector
- Bytecode compiler supporting the entire **Version 0** language spec.
- Comprehensive error generation in the scanner, parser, and compiler.
- Comprehensive runtime error generation, including source code locations.
- Support for loading code from external files by using modules
- First class functions with support for variadic arguments
- Tail call optimization
- Objects and lists as the basic data structures

Included in Version 0 will also be a standard library. This still isn't really planned out, but it
will include modules for the following facilities:
- filesystem access and manipulation (specifically with a eye toward POSIX I/O)
- stream-based I/O supporting files and pipes
- a subprocess library for running system shell commands/external executables
- POSIX fork and join calls
- `fn.core` will be the core language module, and will include basic functions for working with fn's
  built-in data types as well as the higher order functions, map, filter, and reduce. The
  definitions in `fn.core` are automatically copied into every user module.


### Version 0 Non-Features

The following features are planned for fn, but are deliberately omitted in the first release of
major version 0. This is done to simplify the implementation in order to create a version of fn
which can be used and tested in order to aid in deciding which features are worth keeping/adding.

- Streamlined syntax for using `def` to define functions
- Optional and keyword arguments for functions
- Documentation strings for functions and variables. Additional documentation fields containing
  location of the definition, etc.
- Compile-time code generation via macros
- A condition signaling system
- The ability to precompile fn modules to bytecode, and import them as if they were source files
- A smarter repl which supports expressions split over multiple lines
- An emacs extension which connects fn source buffers to a repl
- As much static type verification as we can feasibly do (should be a lot)
- Standard library functions for threading and parallelization
- Standard library functions for linear algebra and scientific/numerical computing
- A standardized hierarchy for holding library modules and a package manager to install modules
  there
- A user-accessible FFI and C++ library files which can be used to aid in working with fn values or
  embedding the interpreter (C++20 required for the headers)
- A minimal set of C bindings for working with fn values. This will expose a subset of the
  functionality of the C++20 library specifically for programming environments where C++20 is not
  available.

**Note:** This is far from an exhaustive list, especially since the later versions of fn


## why tho?

The tagline for fn is that it's LISP with the convenience of Python. Really, that's a lie, since
Python was made by grown-ups, but I think it does a good job conveying the kind of thing I'm going
for.

Just being LISP is really the main feature of fn. I like parentheses and prefix notations and
metaprogramming. However, for various reasons, I've found that all reified LISPs feel somewhat
unwieldy for the kinds of quick-and-dirty things I'd like to use them for. Life is too short to
program in bash, so I usually end up using Python.

Of course, then I'm stuck programming in Python, which is FINE I GUESS, but I kept feeling like
there was a lot of lost potential in LISP. What really convinced me of this, however, was Rich
Hickey and Clojure. I don't know the man myself, but I've read a lot of his writing and watched some
of his more famous talks. I think Clojure is **awesome**. It really shows what you can accomplish by
adding the right features onto a Lisp base.

*So why not just use Clojure?* I hear you asking. Well, for one thing I get impatient waiting for
the JVM to start. More importantly, I don't really want a purely functional programming language,
but a more general-use one which just happens to have good support for functional programming. While
the syntax of fn may look like Clojure, its semantics, particularly its object model, are actually
much more similar to those of Lua and Javascript.

It took me many years of iterative redesigns before I settled on the current set of features. It's
still early in the implementation, but I like what I've come up with.


# Language Intro

`fn` is a language I made specifically because I wanted to program in LISP in more places, so the
target audience is people who already know that they like LISP (or at least the idea of LISP). With
that in mind, this section is written assuming you know at least the basics of some real dialect of
LISP, e.g. Common Lisp, Scheme, Racket, or Clojure.

### This section is under construction :)

Stay tuned.
