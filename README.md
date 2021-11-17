# Fn, a programming language

**Fn** is a programming language which I am currently making for myself.

Language documentation is provided in the file lang-spec.org (in Emacs org-mode
format). Documentation is a work in progress.

The rest of this README contains dubious claims about upcoming features,
disorganized notes about development status, and instructions for building Fn.

## About Fn

Fn is a highly idiosyncratic language consisting of ideas I've taken from
other popular languages, plus a bunch of syntactic conveniences and an object
model of my own contrivance. The most prominent influences present in Fn are
Common Lisp and Python, although there are also ideas taken from Clojure,
Haskell, Lua, and Javascript.

Fn has the following primary goals:
- **Dynamic Functional Programming** - Make a dynamically typed programming
  language suitable for functional programming. This gives the expressive power
  of functional programming without the burden of mandatory type annotations.
- **Functional Programming in 2 Minutes or Less** - Unlike many functional
  programming languages, Fn is meant for general utility scripting as opposed to
  just application development. I want it to be easy to write and run Fn code
  without having to set up a project and a build system first.
- **Modernize Lisp** - Listen, you know those Lisp fellas must've been onto
  *something*, what with all the hullabaloo. I've tried to make a very
  convenient, modern language from a Lisp base, including preserving the
  venerated macro system from Common Lisp. I think it's got heart.
- **Good code management** - Fn's system of packages and namespaces makes it
  easy to include dependencies both external and internal, while requiring very
  little configuration.
- **Portability** - Building Fn depends only on a C++17 compiler and the
  standard library. Future versions will run on Linux and POSIX-compliant OSes,
  Windows, and I'm even gonna try to get it going on bare metal (on some ARM
  MCUs)!
  
Anyway, for all the marketing copy above, I'm really just making this
programming language for myself. It's a weird language. It'd be crazy if anyone
else wanted to use it.


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
         (if (= (mod x 2) 1)
             (set! acc (+ acc 1)) ; update acc for odd number
             nil))                ; otherwise just return nil
       list))
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

The big goal right now is to get the entire core language implemented. It's
actually already *mostly* implemented, but I've decided to rewrite the entire
compiler, because I think a different architecture would better accommodate
macros. I'm also improving the error handling and tidying up the parser while I
go. This is happening in the llir-rewrite branch.

I can't give reliable time estimates, but the rewritten compiler is nearly in a
functional state (albeit one that only supports a subset of Fn). The virtual
machine and allocator are pretty much done, so once I get the compiler sorted
out I'll just be on to the standard library.


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

Fn officially supports x86_64 Linux environments. That's all I'll promise. It's
developed and tested on Arch Linux, although I've successfully built and run it
on macOS a couple of times out of curiosity. I'm trying to make it support any
mostly UNIX-like environment, which means in theory it should build and run on
everything from OpenBSD to macOS to Windows with MinGW.
