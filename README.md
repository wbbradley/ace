# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

## About Zion
- imperative
  - strict (deterministic ordering of operations)
  - impure by default (with purity available)
  - caters to sequential statements and special control flow forms (like `for`,
    `if`, `while` as much as it does to expressions)
- functional
  - first-class anonymous functions
  - capture by value (NB: types can define their internal mutability)
- strongly-typed
  - unsafe casts are possible, but conspicuous
- statically type-checked
  - support for compile-time type-expression evaluation
  - types are/can be inferred
  - polymorphism
    - parametric
    - ad-hoc (though currently w/o type classes)
- algebraic data types
  - product types
  - sum types
  - pattern matching with robust coverage analysis
- some-cost abstractions
  - garbage collection
  - deterministic destruction via
    - `with` special-form
    - `defer` for block-level determinism

Zion prefers correctness and refactorability over performance. Zion targets scenarios where
scalability is intended to happen horizontally, not vertically. That being said, Zion tries to be
fast, using static compilation. Zion is built on [LLVM](https://llvm.org/).

## User Roles
 - In industry there are two primary archetypes of programming language users, Workers and
   Librarians. Experienced developers wear either of these hats, switching back and forth as
   necessary. This switching can happen as new dependencies and integrations are initiated and
   completed within the scope of feature or product work.
   - *Workers* build trustworthy applications that solve problems. Workers demand a pleasant and
     ergonomic experience in order to remain focused on reaching their objectives.
   - *Librarians* extend the capabilities of the language by
     - Creating bindings to external libraries,
     - Integrating functionality
     - Exposing extrinsic data via serialization that maintains type-safety and ergonomics to the Workers.

## Goals

 - Keep it simple, when possible.
 - Keep it readable, when possible.
 - Make algebraic data types compelling for state modeling.
 - Reduce pain near process boundaries. Be opinionated on how to make serialization as comfortable
   as possible while retaining static type safety.

## Non-goals
 - Solving heavy compute/FPU problems is a non-goal in the near future. Solve those problems at a lower
   level, and use the FFI to access those components. Favor algorithmic scaling over bit twiddling
   and fretting over L1 cache hits.
 - Pause-free execution remains a back-burner goal. (ie: Enabling Game loops, high-speed trading
   platforms, life support monitoring, embedded systems, etc...) However, in a future version, once
   language development settles down a bit, this will get more attention.
 - In-language concurrency and asynchronicity will get treatment in the future. These can currently
   be solved at other levels of the stack (hint: use OS processes.) This is not because it's not
   important. But, basic ergonomics of the language come first.

## Syntax Examples

Zion syntax is in the C family.

```
module hello_world

fn main() {
	print("Hello, world!")
}
```

Comments use `#`.
```
fn favorite_number(x int) bool {
	# This is a comment
	return x == 12
}
```

Zion is strict by default, not lazy.  Memory is managed using garbage collection.

Types are declared as follows:

```
# Declare a structure type (aka product type, aka "struct")
type Vector2D has {
	x float
	y float
}

type Mammal is {
    Giraffe(name str, age int, number_of_spots int)
    Lion(name str, age int)
    Mouse(fur_color str)
    Bison(favorite_national_park NationalPark)
}

type NationalPark is {
	Zion
	Yellowstone
	Yosemite
}
```


### Development Environment Setup

You'll need LLVM. Zion is using the `release_40` checkpoint of LLVM currently. The `llvm-build.sh`
script will clone the LLVM (and clang, etc...) sources from github onto your machine, and try to
build them. It will attempt to install into the `$HOME/opt/llvm/release_40/{MinSizeRel,Debug}`
directories. Theoretically you only need the `MinSizeRel` build to work with, however it can be
useful to drop into the `Debug` build from time to time. That will require repointing your config as
appropriate.


Add the following to your `.bashrc` (or equivalent), assuming you have cloned Zion into `$HOME/src/zion`:
```
ZION_SRC="$HOME/src/zion"
LLVM_DIR="$HOME/opt/llvm/release_40/MinSizeRel"
export PATH="$LLVM_DIR/bin:$PATH"
export LD_LIBRARY_PATH=$LLVM_DIR/lib:$LIBRARY_PATH
export ZION_PATH=$ZION_SRC:$ZION_SRC/lib:$ZION_SRC/tests:$HOME/var/zion
```
