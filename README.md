# Fn, a programming language

**Fn** is a programming language which I am currently making for myself.

Language documentation is provided in the file fn-manual.org (in Emacs org-mode
format). Documentation is a work in progress.

The rest of this README contains dubious claims about upcoming features,
disorganized notes about development status, and instructions for building Fn.


## About Fn

Fn is a dynamically typed functional programming language in the Lisp family.
This is not a toy language. I put a lot of careful consideration into practical
issues like performance and FFI, and I fully intend to use Fn for serious
projects. However, Fn is also not finished. While the core language is 95%
implemented, a great deal of work still remains, particularly in the development
of the standard library.

The key features of Fn are:
- **It's Mostly Functional** - We've got built-in persistent data structures,
  immutable global variables, composition-friendly syntax. But Fn also won't try
  to fight you if you need to write some imperative code.
- **It's Dynamically Typed** - Dynamic typing allows expressive data
  representations and intuitive runtime polymorphism.
- **It's Lightweight** - Fn is meant for general utility scripting as well as
  application development. It's easy to write and run Fn code without having to
  set up a project or build system first.
- **It's Modular** - Fn's system of packages and namespaces makes it easy to
  manage dependencies both external and internal, while requiring very little
  configuration.
- **It's Lispy** - You know those Lisp fellas must've been onto *something*,
  what with all the hullabaloo. Fn uses the classic parenthesized syntax from
  Lisp, and preserves the venerated macro system from Common Lisp.

Fn is a strange programming language that will not be for everybody.


## A Brief Example

Here's a tiny preview to give you the general flavor of Fn.

```
; comments start with ;

; global definition (global variables are immutable)
(def x 27)
x  ; = 27

; all operations are of the form (OPERATOR ARGS ...)
; for example, arithmetic is like this:
(+ 3 4)             ; = 7
(* 2 6 2)           ; = 24
(+ (* 2 3) (- 6 4)) ; = 8, read as (2 * 3) + (6 - 4)

; global function definition
(defn count-odd-numbers (list)
  ; Define a local recursive function to count odd numbers.
  ; Fn does tail call optimization, so this is as fast as a loop.
  (letfn iter (acc remaining)
    ; cond is like an if..elseif block
    (cond
      | (empty? remaining)
        acc
      | (= (mod (head remaining) 2))
        ; update accumulator with a tail call
        (iter (+ acc 1) (tail remaining))
      | yes
        (iter acc (tail remaining))))
  (iter 0 list))
```

Obviously there are a lot of features that we don't have space to discuss here!
This information can be found in the language reference manual I'm writing. An
early WIP version of the manual is in the file lang-spec.org, but it's not
totally up to date. I'll update this README when that file is ready for public
consumption.


## Development Status

This is a one man show, and I have a busy life. Progress happens at
unpredictable intervals. However, I'm in it for the long haul. You can check the
github history if you don't believe me.

- The core language is fully implemented.
- The standard library is virtually nonexistent
- Support for foreign data structures is still in its design phase
- The foreign function interface is there but needs a big refactor before it
  will be usable outside of Fn's codebase.
- A few advanced language features such as pattern matching are planned to be
  added before the first proper version of Fn.


## Building Fn

Fn is built with CMake. It depends on a C++ compiler supporting C++20 and the
C++ standard library. With these in hand, one can do:

    git clone https://github.com/jepugs/Fn
    mkdir Fn-build
    cd Fn-build
    cmake ../Fn
    make

This results in the main executable being created as `Fn-build/src/Fn`. You can
give 'er a `$ make install` too, if you're into that, which seems to work but
I'm not sure if I set it up correctly.


## Compatibility

Fn officially supports Linux and POSIX-compliant operating systems on x86_64.
- Development is done on Arch Linux.
- Fn should work in any vaguely UNIX-y environment. I think that right now it
  even works on Windows.
- Building Fn depends only on CMake and a compiler supporting C++20 (e.g. g++,
  clang).
- Building the tests requires Boost.
  

