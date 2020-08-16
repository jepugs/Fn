# fn, a programming language

**fn** will be a general purpose programming language with LISP-style syntax. The goal is to create
a minimalist core language with coherent syntax and predictable, extensible semantics. Features
include:

- an intuitive and tightly-integrated object system
- functions, types, protocols, and modules are first class objects, allowing a great deal of
  flexibility at runtime
- powerful metaprogramming facilities inspired by Common Lisp
- many parentheses

A long, rambling, and incomplete description of fn is in the file [design.org](./design.org).


## Building fn

In order to build fn, you must have [autoconf](http://www.gnu.org/software/autoconf/),
[make](http://www.gnu.org/software/make/), and [SBCL](http://sbcl.org/) installed on your computer.

fn may be built by running the following commands in the source directory:

```
autoconf
./configure
make
```

fn has only been tested on 64-bit Linux operating systems.


## Development status and plans

Version 0 of fn is currently in development. At the time of writing, fn should be considered
incomplete software and should probably not be used for any purpose.

fn is a personal project, but I take it very seriously. I aim to make the language and tools
professional-grade programming language.

I am in the process of writing thorough unit tests for the interpreter. I'm also constantly working
on cleaning up the code base, as thhe rapid pace of development has led to some inconsistencies.

At the moment, the core language specification is nearly stable, but it is not fully documented.
Version 0 will be released once the core language is stable and fully realized via an unoptimized
tree-walking interpreter. The version 0 release series will focus on improving the performance and
error reporting of the interpreter while expanding the standard library. Breaking changes to the
core language will continue to happen during this release series.

A version 1.0 release is far away down the line, and will involve writing a high-performance VM in
C++.


# Language Basics

This section briefly introduces fn and highlights a few of its features. It is by no means a
complete guide to the language.

## Syntax

fn is a member of the Lisp family, and as such has very simple syntax.

There are two basic forms of expressions in fn: **atoms** and **lists**. An **atom** is a single
value, such as a variable name, a number, a string, or a boolean. A **list** is a sequence of
expressions within a pair of parentheses. The first element in the sequence is called the
**operator** and the others are the **arguments**.

```
;; comments begin with one or more semicolons and end with newlines
;; The only other role of whitespace is to separate expressions

;; examples of atoms
23
-6
27.0
"Hello, World!"
true
false
null

;; every list has this form:
(operator argument1 argument2 and-so-on)

;; example lists using built-in arithmetic operators
(+ 3 4)   ; = 7
(/ 8 2)   ; = 4
;; arithmetic operators accept arbitrarily many arguments
(+ 1 2 3) ; = 6
;; lists can be nested
(* (- 2 4) 17) ; = -34

;; lists may also have side effects
(print "Hello, world!") ; prints to STDOUT
```

All other syntax is really just sugar; in fact, the interpreter converts all expressions to list
and atom syntax before evaluation. For example, fn provides dot syntax which expands as follows:

```
object.field
;; is expanded to
(get object (quote field))
```


## Control flow

fn's control flow primitives are the special operators `do`, `if`, `cond`, and `case`.

```
;; do evaluates a series of expressions in order
(do
  (print "What's up, world?")
  ;; String creates a string from its arguments
  (print (String "Two plus two is " (+ 2 2)))
  (print "Goodbye, globe!"))

;; if takes three arguments: a condition, a then clause, and an else clause:
(if (= 2 4)                    ; condition
    (print "math is broken")   ; then clause (not evaluated because condition is false)
    (print "everything's ok")) ; else clause
    
;; for the purposes of if, every value except false and null is considered true
(if true 1 2)  ; = 1
(if false 1 2) ; = 2
(if null 1 2)  ; = 2
(if 17 1 2)    ; = 1

;; cond is similar to if, but it takes arbitrarily many condition-and-result 
;; pairs for arguments. cond returns the result of the first condition that 
;; succeeds. (Lisp programmers should note that this version of cond has fewer
;; parentheses than usual).
(defvar x 17)
(cond
  (even? x) "x is even" ; even? checks if a number is even
  true      "x is odd")
;; = "x is odd"
(set x 18)
(cond
  (= x 18)  "x is eighteen" ; (= x y) checks if x and y are equal
  (even? x) "x is even"
  true      "x is odd")
;; = "x is eighteen" because only the first result is used
```

`case` performs pattern matching and is tightly coupled to fn's object system, so we decline to
discuss it here.

## Variables and functions

Various special forms exist to create global variables.

```
;; def is a special operator used to create variables. This is an abuse of 
;; terminology, as these "variables" are actually constants by default
(def x 17)
x ; = 17
;; set is used to assign values. In this case, it emits an error because x is 
;; constant
(set x 18)

;; defvar creates "true" variables
(defvar y null)
;; so this is legal
(set y "foo")
```

## Higher order functions

This section is a work in progress.

```
;; map, filter, and reduce
(def test-input [23 14 2 -2 6 15 -8 3 20 -12 0 -19])

(map $(+ $ 6) test-input)
;; => [29 20 8 4 12 21 -2 9 26 -6 6 -13]

(filter even? test-input)
;; => [14 2 -2 6 -8 20 -12 0]

(reduce $(+ (* $0 10) $1) [4 0 3 2])
;; => 4032
```
