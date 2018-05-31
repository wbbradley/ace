# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

### Zion features
- imperative (combined with strict, all this really means is that the language
  - strict (deterministic ordering of operations)
  - impure by default (with purity available)
  - caters to sequential statements and special control flow forms (like `for`,
    `if`, `while` as much as it does to expressions)
- strongly-typed
  - unsafe casts are possible, but conspicuous
- statically type-checked
  - types are inferred
  - polymorphic
- algebraic data types
  - supports pattern matching with `match` with robust coverage analysis
- some cost abstractions
  - garbage collection
  - deterministic destruction via ref-counting and/or
    `defer` or `with` behavior (TBD)

Zion prefers correctness and refactorability over performance. Zion
targets scenarios where scalability is intended to happen horizontally, not vertically. That being
said, Zion tries to be fast, using static compilation to native host architecture as its execution
model. Zion is built on [LLVM](https://llvm.org/).

## User Roles
 - In industry there are two primary archetypes of programming language users, Workers and Librarians. Experienced developers wear either of these hats, switching back and forth as necessary. This switching can happen as new dependencies and integrations are initiated and completed within the scope of feature or new product work.
   - *Workers* build trustworthy applications that solve problems. Workers demand a pleasant and
     ergonomic experience in order to remain focused on reaching their objectives.
   - *Librarians* extend the capabilities of the language by
     - Creating bindings to external libraries,
     - Integrating external libraries so as to appear fluid and seamless to the Workers,
     - Exposing extrinsic data via serialization that maintains type-safety and ergonomics to the Workers.

## Goals

 - Keep it simple, when possible.
 - Keep it readable, when possible.
 - Make algebraic data types compelling for state modeling.
 - Reduce pain near process boundaries. Be opinionated on how to make serialization as comfortable as possible while
   retaining static type safety.

## Non-goals
 - Solving heavy compute problems is a non-goal. Solve those problems at a lower level,
   and use the FFI to access those components. Favor algorithmic scaling over bit twiddling and fretting over L1 cache hits.
 - Pause-free execution remains a back-burner goal. (ie: Enabling Game loops, high-speed trading platforms, life support monitoring, embedded systems, etc...) However, in a future version, once language development settles down a bit, this will get more attention.
 - In-language concurrency and asynchronicity will get treatment in the future. These can currently be solved at other levels of
   the stack (hint: use OS processes.) This is not because it's not important. But, basic ergonomics of the language come first.

## Syntax

Zion looks a bit like Python, minus the colons.

```
module hello_world

fn main()
	print("Hello, world!")
```

Comments use `#`.
```
fn favorite_number(x int) bool
	# This is a comment
	return x == 12
```

Zion is strict by default, not lazy.  Memory is managed using garbage collection.

### README TODO
- [ ] discuss namespaces.
- [ ] discuss future plans for safe dynamic dispatch. ([some thoughts exist here](https://gist.github.com/wbbradley/86cd672651cf129a91d14586523e979f))
- [ ] discuss the std library layout.


Types are declared as follows:

```
# Declare a structure type (aka product type, aka "struct")
type Vector2D has {
	x float
	y float
}

# Note the use of the word "has" after the type name. This signifies that the
# Giraffe type "has" the succeeding "dimensions" of data associated with its
# instances.
type Giraffe has {
	name str
	age int
	number_of_spots int
}

type Gender is { Male Female }

type Lion has {
	name str
	age int
	gender Gender
}

type Mouse has {
	fur_color str
}

type NationalPark is {
	Zion
	Yellowstone
	Yosemite
}

type Bison has {
	favorite_national_park NationalPark
}
```

When a call to a function that takes a sum type is made, and there remain free type
variables in the type of the parameters, they will be substituted with the bottom "⊥" type (void) during
function instantiation.

### Getting LLVM built on your Mac

```
./llvm-build.sh
```
Then add $HOME/opt/llvm/release_40/MinSizeRel/bin to your path. Be sure to add it before any existing versions of clang
or llvm tools, etc...

### TODO

- [ ] Ergo: Ability to import symbols from modules by name (symbol injection)
- [x] Implement closures with capture by value
- [ ] Ergo: Implement tuple matching
- [ ] Func: Implement string matching
- [ ] Perf: Implement native structures as non-pointer values
- [ ] Ergo: Consider uniform calling syntax for .-chaining and how to acheive customizable monadic behaviors by looking at the receiver. This would also entail some magic in terms of modules.
- [ ] Perf: Escape analysis to avoid heap-allocation.
- [ ] Play: Rewrite expect.py in Zion
- [ ] Consider how to allow for-macro expansion to have a mutating iterator function. Does that mean pass-by-ref is allowed?
- [ ] Consider making all refs managed/heap-allocated (prior to a later escape-analysis test) in order to allow reference capture... maybe.
- [ ] Consider type-classes
- [ ] Implement fast range(i)
- [ ] Use DIBuilder to add line-level debugging information
- [ ] Implement an inline directive to mark functions for inline expansion during optimization
- [x] Add safety checks on casting (as)
- [ ] Implement generic sort
- [x] Exercise: implement parser combinators in Zion
- [ ] Consider allowing overloads for arbitrary `tk_operator`s to enable overloading random symbols.
- [ ] Data structures
  - [x] string (as slices)
  - [x] vectors
  - [ ] hash map
  - [ ] set
  - [ ] binary tree
  - [ ] avl tree / red-black tree
- [ ] decide on `with` (Python) / `using`(`dispose`) (C#) / 'defer' (Golang) style syntax for deterministic destruction - or ref-counting
- [ ] Rework debug logging to filter based on taglevels, rather than just one global level (to enable debugging particular parts more specifically)
- [ ] Enable linking to variadic functions (like printf)
- [ ] Fix linking issues (rt_float.o, etc...) when running zion from non-zion root dir)
- [ ] Rename `typeid` function to `ctor_id` or something similar.
- [ ] Automatically configure default POSIX/C/System "int" size on first boot of compiler
- [ ] Explore using a conservative collector
- [ ] Integrate JSON parsing and mess around with manipulating some existing JSON files
- [ ] Expose reflection library for dynamic structure analysis
- [ ] Pattern-matching
  - [x] ctor matching
  - [ ] int matching
  - [x] coverage checking for ctors
  - [ ] coverage checking for ints
- [x] Implement backtracking in unification of product types
- [x] Consider marking null-terminated strings differently for FFI purposes (ended up doing this as part of "safe-unboxing" for easier FFI.
- [x] Implement slice array indexing rg[s:end], etc...
- [x] Implement vector slicing for strings and arrays
- [x] Optimize `scope_t`'s `get_nominal_env` and `get_total_env` to be cached
- [x] Maintenance: Change all `status_t` parts of compiler to use exceptions
- [x] Check for duplicate bound function instantiations deeper within function instantiation
- [x] Change := to be let by default
- [x] (un)signed integers
  - [x] write a C integer promotion compatibility test as part of test framework
  - [x] integers as a type with parameterized number of bits and whether to use
    sign-extend or zero-extend
  - [x] promotions upon binary operators
  - [x] prevent overloading integer operations unless one side is not an integer
  - [x] deal with casting integers
- [x] implement `let` declarations
- [x] change `str` to use `wchar_t` as its underlying physical character type
  - [x] use C99's `mbstowcs` and `wcstombs` to convert back and forth
  - [x] propagate usage of utf8 for `char`
- [x] 'for' syntax - based on `tests/test_list_iter.zion` pattern
- [x] Ternary operator
- [x] Logical and/or (build with ternary operator)
- [x] Type refinements for ternary / conditional expressions
- [x] Implement vector literal checking and code gen
- [x] Design/Implement tags functionality (for integration with ctags and LSP)
