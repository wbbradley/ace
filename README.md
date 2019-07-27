# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

## Quick Start

To play with Zion in Docker, try this.

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

Zion is a statically typed procedural/funct onal language. It is a work in
progress. Please reach out if you'd like to get involved.

### Syntax

```
fn main() {
  print("Hello world.")
}
```

The syntax resembles C or Python (with braces.) The type system is
based on System F with extensions for Type Classes, newtypes and pattern
matching. There is no macro system but there is a rich syntax
available via reader macros within the parser.

### Semantics

The evaluation of Zion is strict, not lazy. The call-by-value method of passing
arguments is used.

### Mutability

There is no explicit notion of immutability, however it is implicit unless
`var` is used. `var` declarations wrap initialization values in a mutable
reference cell. Under the covers, this is the `std.Ref` type. The primary way
to maintain mutable state is to use `var`.

### Encapsulation

There is no class-based encapsulation in Zion. Encapsulation can be achieved by
not letting local variables escape from functions (or blocks), or by using module-local
functions.

### Type System

Types are inferred but type annotations are also allowed/encouraged as
documentation and sometimes necessary when types cannot be inferred.  Zion
does not support default type class instances by design (although, if a good
design for that comes along, it might happen.)

### Polymorphism

Polymorphism comes in two flavors.

Type-based polymorphism exists at compile time in the ability to use type
variables which are re-bound per function invocation. At run-time, there are a
couple different notions of polymorphism. First, Zion supports sum types by
allowing the declaration of types with multiple data constructors.  This then
relies on `match` statements (pattern matching) to branch on the run-time
value. This form of polymorphism may feel unfamiliar to folks coming from "OOP"
languages that rely on inheritance and/or abstract classes with any number of
derived implementations.

Since Zion treats functions as values and allows closure over function
definitions (`fn`), you can `return` new behaviors as functions. Users of those
functions will get statically checked run-time varying behavior (aka run-time
polymorphism). For example, the `Iterable` type class requires a single
function which will itself return a function which can be called repeatedly to
iterate. It has a signature like

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
the target host.

The best way to learn more at this time is to read through the
`tests/test_*.zion` code.
