# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

## Quick Start

To quickly play with Zion in Docker, try this.

```
git clone https://github.com/zionlang/zion.git
cd zion

# Get a docker image set up ready to run a build
./docker-build.sh && ./docker-run.sh bash

# The prior command should open up a bash prompt within a new docker container.
# Build and install Zion inside this container.
make install

# The prior command should have installed Zion to /usr/local. Set up the proper
environment variables.
export ZION_PATH=/usr/local/share/zion/lib
export ZION_RT=/usr/local/share/zion/runtime

# Build and run a simple test program
cd
echo 'fn main() { print("Hello world.") }' > hello_world.zion
zion hello_world

# Read more
man zion
```

## Description

Zion is a statically typed procedural/functional language. It is a work in
progress. Please reach out if you'd like to get involved.

### Syntax

```
fn main() {
  print("Hello world.")
}
```

The syntax loosely resembles C or Python (with braces.) The type system is
based on System F with extensions for Type Classes, newtypes and pattern
matching. There is no macro system but there is a relatively rich syntax
available via reader macros within the parser.

### Semantics

The evaluation of Zion is strict, not lazy. The call-by-value method of passing
arguments is used.

There is no explicit notion of immutability, however it is implicit
unles `Ref` is used. `var` declarations automatically wrap the initialization
value in a `Ref`. The primary way to maintain mutable state is to use `Ref`. 

### Encapsulation

There is no class-based encapsulation in Zion. Encapsulation can be acheived by
not letting local variables in a function escape, or by using module-local
functions.

### Type System

Types are inferred but type annotations are also possible and occasionally
necessary when types cannot be inferred.

### Polymorphism

Polymorphism comes in two flavors.

Type-based polymorphism exists at compile time in the ability to use type
variables which can be bound differently per invocation of a function. At
run-time, there are a couple different notions of polymorphism. First, Zion
supports sum types by allowing the declaration of types with multiple different
data constructors. This then relies on `match` statements (pattern matching) to
branch on the run-time value. This form of polymorphism may feel unfamiliar to
folks coming from "OOP" languages that rely on inheritance and "abstract"
classes with any number of derived implementations.

Since Zion treats functions as values, and allows closure over `fn`
definitions, you can `return` new behaviors as functions, and then the users of
those functions will be using something whose behavior may vary at run-time,
but whose type is static. For example, the `Iterable` type class requires a
single function which will itself return a function which can be called
repeatedly to iterate. It has a signature like

```
class Iterable a {
  fn iter(a b) fn () Maybe b
}
```

So, on top of the type being returned by the iterator being compile-time
polymorphic, the usage of such `Iterables` at run-time also involves a run-time
closure that may have any number of behaviors or "shapes" in how it operates.
Thus, it is polymorphic, but conforms to the specification that it must return
a `Maybe` type. In this case, the `Maybe` type has two data constructors,
`Just` and `Nothing`. If an iterator returns `Nothing`, it indicates that it is
done iterating.

All code that is reachable from `main` is specialized and monomorphized prior
to the final code generation phase. Code generation creates LLVM IR, which is
passed through clang to perform static linking, optimization, and lowering to
the target host. There is no polymorphism at runtime outside of passing
functions by value.

The best way to learn more at this time is to read through the
`tests/test_*.zion` code.
