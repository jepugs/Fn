# Fn Project Postmortem

This postmortem is mostly for my own reflection, but I think it might be
instructive or at least mildly interesting for others as well.

Fn was my first serious attempt at making a programming language. At the time of
writing, it is also the biggest program I have ever written by myself. As you
can see from the original README preserved in this repository, I had very big
plans for Fn. As you can see from the git history, I abandoned the project
suddenly after finishing most of the first version.

## Why make a programming language?

Programming language design and implementation have been interests of mine for a
very long time. Early on in my programming career, I made several unsuccessful
attempts to design and implement programming languages before realizing that it
was simply beyond my capabilities at the time.

As the years went on and my skills developed, this goal began to seem much more
attainable.[^2] I began Fn as a side project in 2018, and chipped away at it in
small pieces for the next few years.

This motivation belies the first major deficiency in my plan for Fn: I didn't
have a use case. I just wanted to make a language.

[^2]: Point in case: the code in this repository. There's a whole interpreter
    here, with a stack VM, a bytecode compiler, and a compacting, generational
    garbage collector of which I'm particularly proud.

## Development History

I began work on Fn during the summer of 2018, although for the first two years I
would mainly be working on improving my programming skills and understanding of
the domain. During this time I worked through most of Bob Nystrom's book
[Crafting Interpreters](http://www.craftinginterpreters.com), which I cannot recommend
highly enough.

I mocked up an early prototype in Common Lisp using macros and readtables. As
far as I know the code for this prototype is lost to time. This is probably for
the best; I wrote a lot of embarrassing Lisp code in college.

Eventually I began work on the C++ version of the interpreter. While I did spend
a considerable amount of time learning about programming language
implementations prior to this, I did very little planning ahead of writing the
code for the interpreter.

There were a couple false starts, and work on Fn may have fizzled out altogether
in 2020 had it not been for the COVID-19 pandemic. The pandemic led me develop a
general sense of dissatisfaction that I sought to escape by spending hours each
day working on Fn. This incurred a considerable personal and academic cost for
me, as I neglected my other responsibilities to work on a crackpot programming
language.[^3]

The development methodology I had chosen was:
1. Wing it until there's a skeleton of the program in place.
2. Add features to the skeleton one step at a time.

This is a very fun way to program, especially if you're not beholden to any
standards of quality. You get lots of little dopamine hits from checking
newly implemented features off the list. If your project is small enough, I
actually think this approach works pretty well, and it's a good way to learn.

The problem is that without any forethought to the overarching program
architecture, you can end up in a situation where it's really difficult to
implement an important feature. So you end up either doing a big and expensive
refactor/rewrite to accommodate the feature, or you add some inelegant hack to
do it without addressing the architectural issues. Hacks like this make code a
lot harder to read or modify without breaking something, and if they pile up
too much your project is basically screwed.

These problems caused a lot of trouble throughout the development process, and
were compounded by my lack of experience with C++ or software engineering in
general.

Even so, by the end of 2021, I had made good progress on the implementation,
although the source code had grown unruly. Development would continue on and off
until May 2022. At this point, most of the language was already implemented.

The problem was that I turned out to hate the language that I had designed. I
decided to go back to the drawing board and do a complete rewrite of both the
spec and the interpreter. None of this work is recorded in the git history. I
finally stopped work on Fn in September of 2023. I like a lot of the ideas I
came up with in that last year, but by then I was just tired.

All told, the Fn project was active for just a bit over 5 years, and never even
produced so much as a release candidate.

[^3]: On the other hand, the skills and knowledge I gained during this time are
    of immeasurable value to me. So it's a mixed bag.

## Mistakes

The key factors I believe caused Fn to fail are:
1. Absence of a clear use case
2. Poor development methodology with insufficient time allocated to planning
3. My lack of prior experience with software development and C++
4. Extremely overambitious and constantly-shifting feature set

Some of these factors are related. For instance, the lack of a clear use case
made it difficult to evaluate the usefulness of a given feature. As a result the
feature set grew to include everything I thought I might need -- worsening the
shifting and unrealistic requirements, which in turn informed the development
methodology.

In retrospect, the failure of Fn seems like it was inevitable. It wasn't exactly
hard for me to pick out the problems listed above. To some extent, I was aware
of these issues fairly early on in the development process.

Fortunately, these problems are fairly straightforward to address (except
arguably problem 2). I hope that this experience will help me to avoid similar
issues in the future.

## Successes

Fn's greatest accomplishment was that it actually existed, at least for a little
while. The parts that were implemented worked. If nothing else, it's an
interesting piece for the portfolio.

The biggest technical achievement in this project is undoubtedly the garbage
collector, which uses a generational region-based compacting algorithm.

The best part about working on Fn was that it allowed me to study in depth some
of the most interesting problems in computer science. In addition, my skills as
a programmer improved substantially while I was working on Fn.

## Conclusion: Was Fn Really a Failure?

For a while, Fn was a big part of my life. I put a lot of my ego into this
project, and it had a significant impact on the person I am today, 5 years
later.

No one will ever use Fn, not even me. My mess of C++ is not a fully realized
program, nor is it salvageable as a component of a new interpreter. There are
dozens of pages of designs and documentation for features I never got around to
adding.

Fn is a failure as a software project, and that stings a little bit.

It's not really fair to myself to apply those standards though. Fn was a
personal project. Even at my most confident, I never really expected someone
else to use it. And I'm not a professional programmer, I'm just a lone,
obsessive hobbyist. Fn was never going to be useful in any conventional sense,
even if it was finished, so that's not the metric by which this project should
be judged.

The things I've gained from Fn are intangible but immensely valuable.


