# fn, a programming language

**fn** is a programming language which I am currently making for myself. Its features will include

- A simple class-less object system inspired by Lua and Javascript, complete with dot syntax
- A module system I kinda stole from Python
- Filesystem and I/O facilities conveniently built into the standard library
- A powerful metaprogramming system via macros
- Many parentheses

Version 0 is a minimal prototype of fn which supports most of the core language features. There will
be three parts to the spec: one each for the language, the standard library, and the frontend.
Currently the language spec is done (up to editing and a few missing details) and fully implemented
in the interpreter (up to bug-testing and setting the default module environment). See the file
lang-spec.org for the current draft of the language spec.

## Can I actually use this?

Ok so, like, it works. All the built-in syntax and special forms compile and run as they're supposed
to. We also leak memory right now, though the garbage collector is not that far away. What I'm
saying is, you can probably use it soon. I mean, that's my whole point here. I don't care if anyone
ever reads this README. I'm just sick of Python being the best programming language for what I want
to do. Whitespace is syntax(!!) in Python, for fuck's sake! fn is so far removed from that bullshit,
that it has formalized fucking **syntactic forms**, where we formally define whitespace to do
nothing beyond separating atoms; in all other contexts, it could be deleted. Plus the lambda
semantics in fn are Good and Righteous. With closures, you can make private member variables, no
problem! All I'm saying is, I designed fn exactly the way I want it. It's simultaneously
object-oriented and functional, both in the worst possible way.

## Building fn

Build fn by running [make](http://www.gnu.org/software/make/) from the source directory. This will
result in the creation of a self-contained executable called fn. You will also need a version of
[g++](https://gcc.gnu.org/) that supports `-std=C++20`.


### Compatibility

fn officially supports x86_64 Linux environments. That's all I'll promise. It's developed and tested
on Arch Linux, although I've successfully built and run it on macOS a couple of times out of
curiosity. I'm trying to make it support any mostly-POSIX-compliant environment, which means in
theory it should build and run on everything from OpenBSD to macOS to Windows with MinGW.


## Development plan

fn is a personal project, but I take its quality seriously. That said, it's not quite ready for
production use.

To aid in the implementation of fn, I started by speccing out a somewhat less ambitious version of
the language called **Version 0**. This will really come in two pieces, the first when I'm done
implementing the core language (which I'm basically done doing) and the second after I've added a
small standard library. The timeframe for these additions could be anywhere from days to months
depending on how much I work on this project, but I'm very close.

After version 0 is feature complete, I will do one big refactoring/review pass on the whole project
and then write a thorough suite of unit and integration tests. At this point I should have something
you could use in a production environment, so I'll release it as version 0.1.0 and try to document
everything. This will likely mark a hiatus in the development of fn, as I try writing some
medium-sized programs with it to see how it feels.

After the 0.1.0 release, the majority of the work will shift to implementing macros, improving the
module system, and adding to the standard library. Other important features which are planned just a
bit further down the line include an interactive breakpoint debugger, the ability to precompile
modules to bytecode, multithreading, dynamic variables, pattern matching, and an FFI. I'd also love
to add static type checking and/or JIT compilation, but those are both pretty heavy features that
would take more development time than I probably have.


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
