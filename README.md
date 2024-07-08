# Ace Language


[![Tests](https://github.com/wbbradley/ace/workflows/Tests/badge.svg)](https://github.com/wbbradley/ace/actions?query=workflow%3ATests)

## Fundamentals

Ace resembles a combination of Haskell and C, with garbage collection, eager
evaluation, static type-checking, purity and impurity (when you want it), extensible infix operators,
type-classes to allow ad-hoc polymorphism, `with` control-flow semantics for bracketing resource usage, pattern-matching, and type inference.

## Quick Start

To play with Ace in Docker, try this.

```
git clone https://github.com/wbbradley/ace.git
cd ace

# Get a docker image set up ready to run a build (assumes Docker is running).
./docker-build.sh && ./docker-run.sh bash

# The prior command should open up a bash prompt within a new docker container.
# Build and install Ace inside this container.
make install

# The prior command should have installed Ace to /usr/local. Set up the
# $ACE_ROOT environment variable.
export ACE_ROOT="/usr/local/share/ace"

# Build and run a simple test program
cd
echo 'fn main() { print("Hello world.") }' > hello_world.ace
ace hello_world

# Read more
man ace
```

## Description

Ace is a statically typed procedural/functional language. It is a work in
progress. Please reach out if you'd like to get involved.

### Syntax

```
fn main() {
  print("Hello world.")
}
```

The syntax resembles C or Python (with braces.) The type system is based on
[System F](https://en.wikipedia.org/wiki/System_F) with extensions for Type
Classes, newtypes and pattern matching. There is no macro system but there is a
rich syntax available via reader macros within the parser.

#### Examples

##### Deterministic cleanup

```
fn main() {
  let filename = "some-file.txt"
  # 'with' gives guarantees that the value can clean itself up. See std.WithElseResource.
  with let f = open(filename) {
    for line in readlines(f) {
      print(strip(line))
    }
  } else errno {
    print("Failed to open ${filename}: ${errno}")
  }
}
```

##### For comprehensions and iterators

```
import itertools {zip}

fn main() {
  # Multiply some zipped Ints and put them into a Vector
  print([x * y for (x, y) in zip([1..3], [4..])])
  # prints [4, 10, 18]...
}
```

##### Lambdas (anonymous function expressions)

```
fn main() {
  let double = fn (x) => x * 2
  assert(double(25) == 50)
}
```

##### Lambda shorthand

```
fn main() {
  let double = |x| => x * 2
  assert(double(25) == 50)
}
```

### Semantics

The evaluation of Ace is strict, not lazy. The call-by-value method of passing
arguments is used.

### Mutability

There is no explicit notion of immutability, however it is implicit unless
`var` is used. `var` declarations wrap initialization values in a mutable
reference cell. Under the covers, this is the `std.Ref` type. The primary way
to maintain mutable state is to use `var`.

```
fn main() {
  # Create a value with let. By default it is immutable.
  let y = 4
  # Try to change it...
  y = 5          // error: type error

  # Create a variable with var. It is mutable.
  var x = 5
  print("${x}")  // Prints "5"

  # Change what is in the memory cell described by x...
  x = 7
  print("${x}")  // Prints "7"

  # Try putting some other type of thing in there...
  x = "hey!"     // error: type error. Int != string.String
}
```


### Encapsulation

There is no class-based encapsulation in Ace. Encapsulation can be achieved by

1. using modules to implement Abstract Data Types, exposing only the functions
   relevant to the creation, use, and lifetime of a type.
2. not letting local variables escape from functions (or blocks), or by using
   module-local functions.

#### Modularity

Ace lacks support for shared libraries or any shareable intermediate
representation. Code complexity and leaky abstractions can still be avoided by
limiting which symbols are exposed from source modules.

### Type System

Types are inferred but type annotations are also allowed/encouraged as
documentation and sometimes necessary when types cannot be inferred. Ace
rejects [intermediate type defaulting](https://kseo.github.io/posts/2017-01-04-type-defaulting-in-haskell.html) by design. Although, if a good
design for that comes along, it might happen.

### Polymorphism

Polymorphism comes in two flavors.

Type-based polymorphism exists at compile time in the ability to use type
variables which are re-bound per function invocation. At run-time, there are a
couple different notions of polymorphism. First, Ace supports sum types by
allowing the declaration of types with multiple data constructors.  This then
relies on `match` statements (pattern matching) to branch on the run-time
value. This form of polymorphism may feel unfamiliar to folks coming from "OOP"
languages that rely on inheritance and/or abstract classes with any number of
derived implementations.

Since Ace treats functions as values and allows closure over function
definitions (`fn`), you can `return` new behaviors as functions. Users of those
functions will get statically checked run-time varying behavior (aka run-time
polymorphism). For example, the `Iterable` type-class requires the definition
of a single function which will itself return a function which can be called
repeatedly to iterate. It has a signature like

```
class Iterable collection item {
  fn iter(collection) fn () Maybe item
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

### Learning more

The best way to learn more at this time is to read through the
`tests/test_*.ace` code.

TODO: struct types do not support pattern matching. proposed solution: eliminate
structs, but add names to newtypes.

[![HitCount](http://hits.dwyl.com/wbbradley/ace.svg)](http://hits.dwyl.com/wbbradley/ace)
