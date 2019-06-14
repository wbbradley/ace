# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

## Quick Start

To quickly play with Zion in Docker, try this.

```
git clone https://github.com/zionlang/zion.git
cd zion

# Get a docker image set up ready to run a build
./docker-build.sh && ./docker-run.sh bash

# Build and install Zion in this Docker instance
make install

# Build and run a simple test program
cd
echo 'fn main() { print("Hello world.") }' > hello_world.zion
zion hello_world

# Read more
man zion
```

## Description

Zion is a statically typed procedural functional language. It is a work in
progress. Please reach out if you'd like to get involved.

The syntax loosely resembles Algol, C or Python with braces. The type system is
based on System F with extensions for Type Classes, newtypes and pattern
matching. There is no macro system but there is a relatively rich syntax
available as hard-coded reader macros within the parser.

The evaluation of Zion is strict, not lazy.

The call-by-value method of passing arguments is used.

There is currently no explicit notion of immutability, however it is implicit
unles `Ref` is used. `var` declarations automatically wrap the initialization
value in a `Ref`. The primary way to maintain mutable state is to use `Ref`. 

Types are inferred but type annotations are also possible and occasionally
necessary when types cannot be inferred.

Polymorphism relies on a couple things. First, functions can be passed by
value, so to get a new behavior from a piece of code, you can pass in that
behavior as a function. 

All code that is reachable from `main` is specialized and monomorphized prior
to the final code generation phase. Code generation creates LLVM IR, which is
passed through clang to perform static linking, optimization, and lowering to
the target host. There is no polymorphism at runtime outside of passing
functions by value.

The best way to learn more at this time is to read through the
`tests/test_*.zion` code.
