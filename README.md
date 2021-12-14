# Fn, a programming language

**Fn** is a programming language which I am currently making for myself.

Language documentation is provided in the file fn-manual.org (in Emacs org-mode
format). Documentation is a work in progress.

The rest of this README contains dubious claims about upcoming features,
disorganized notes about development status, and instructions for building Fn.


## About Fn

Fn is a dynamically typed functional programming language in the Lisp family.
This is not a toy language. It's meant to be used, if only by me. Fn is very
much a work in progress. While the language is fully implemented, a great deal
of work still remains in writing tests, developing the standard library, and
improving error generation.

The key features of Fn are:
- **It's Mostly Functional** - Fn aims to be an excellent building material for
  functional programs. Global variables are immutable, as are lists and strings.
  However, Fn provides mutable tables, as well as allowing functions to store
  state via variable capture. The result is a language suited for functional
  programming which doesn't fight you when you need to make an exception.
- **It's Lightweight** - Fn is meant for general utility scripting as well as
  application development. It's easy to write and run Fn code without having to
  set up a project and a build system first.
- **It's Dynamically Typed** - Dynamic typing allows expressive data
  representations and intuitive runtime polymorphism.
- **It's Modular** - Fn's system of packages and namespaces makes it
  easy to include dependencies both external and internal, while requiring very
  little configuration.
- **It's Lispy** - You know those Lisp fellas must've been onto *something*,
  what with all the hullabaloo. Fn uses the classic parenthesized syntax from
  Lisp, and preserves the venerated macro system from Common Lisp.

Anyway, for all the marketing copy above, I'm really just making a programming
language for myself. It'd be cool if someone else liked it though!


## A Brief Example

Here's a tiny preview to give you the general flavor of Fn.

```
;; comments start with ;

;; global definition (global variables are immutable)
(def x 27)

;; all operations are of the form (OPERATOR ARGS ...)
;; for example, arithmetic is like this:
(+ 3 4) ; = 7
(* 2 6) ; = 12
(+ (* 2 3) (- 6 4)) ; = 8
;; whereas conditionals are like this:
;; (if <test-expression> <then-expression> <else-expression>)
(if true 1 2) ; = 1
(if false 1 2) ; = 2
;; both nil and false are treated as false
(if false "yes" "no") ; = "no"
(if nil "yes" "no"); ; = "no"
;; any other value is true
(if 69 "yes" "no"); ; = "yes"
(if "false" "yes" "no"); ; = "yes"

;; functions are created with fn
(def square-fun (fn (x) (* x x)))
(square 14) ; = 196
;; short syntax (defn ...) is provided to define global functions
(defn square-fun (x) (* x x)) ; equivalent to previous def

;; local variables are created with let. They are mutable, so we can write
(defn count-odd-numbers (list)
  (let acc 0)
  (map (fn (x)
         (if (odd? x)
             (set! acc (+ acc 1)) ; increment for odd number
             nil))                ; otherwise just return nil
       list)
  acc) ; return acc
;; Here map applies a function to each element of a list

;; However, we'd prefer to write this function in a purely functional way:
(defn count-odd-numbers (list)
  ; Fn supports tail recursion!
  (letfn iter (acc rest)
    (if (empty? rest)
        acc ; return acc at end of list
        (if (= (mod (head rest) 2) 1)
            (iter (+ acc 1) (tail rest)) ; update accumulator
            (iter acc (tail rest)))))
  (iter 0 list))
```

Obviously there are a lot of features that we don't have time to discuss here!
This information can be found in the language reference manual I'm writing. An
early WIP version of the manual is in the file lang-spec.org, but it's not
totally up to date. I'll update this README when that file is ready for public
consumption.


## Development Status

This is a one man show, and I have a busy life. Progress happens at
unpredictable intervals.

The entire language is implemented as described in the manual. There are some
issues with error generation. In particular, the compiler will let you create
variables with illegal names. Most of the CLI frontend is implemented as well.
There's not much in the way of a standard library yet.

Currently I'm working on these four things:
- expanding the set of test cases
- improving the FFI
- designing/writing the standard library
- tweaking the VM and GC for performance

I thought at this point we'd be ready for an 0.1 release candidate, but there's
a lot more I want to do before we're at that point.

In more optimistic news, here are some features which are planned to be added:
- pattern matching (including destructuring in definitions)
- new data types: mutable/immutable vectors and immutable tables


## Building Fn

Fn is built with CMake. It depends on a C++ compiler supporting C++17 and the
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
- Building Fn depends only on CMake and a compiler supporting C++17 (e.g. g++,
  clang).
- Building the tests requires Boost.
  

