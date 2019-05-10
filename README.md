# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

## Work in progress

Zion 1.0 was an exploration of ad-hoc polymorphism with a very complex type system. Zion 2.0 will
use a simpler but more powerful type system (System F with extensions for Type Classes, pattern
matching, etc...). It will use strict semantics and C-style syntax.

More to come...

## Roadmap

- [ ] Evaluation
- [ ] REPL
- [ ] Out of order declarations
- [ ] Supporting implicit recursion without `fn rec` or `let rec x and y ...`
- [ ] Type aliases for convenience `type String = [Char]`
- [ ] Type hiding with `newtype`
- [ ] Return statement coverage checking
