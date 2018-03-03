# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

Zion is a programming language. Zion prefers correctness and refactorability over performance. Zion
targets scenarios where scalability is intended to happen horizontally, not vertically. That being
said, Zion tries to be fast, using static compilation to native host architecture as its execution
model. Zion is built on [LLVM](https://llvm.org/).

## User Roles
 - In industry there are two archetypes of programming language users, Workers and Librarians. Experienced developers wear either of these hats, switching back and forth as necessary. This switching can happen as new dependencies and integrations are initiated and completed within the scope of feature or new product work.
   - *Workers* build trustworthy applications that solve problems. Workers demand a pleasant and ergonomic experience in order to remain focused on reaching their objectives.
   - *Librarians* extend the capabilities of the language by
     - Creating bindings to external libraries,
     - Integrating external libraries so as to appear fluid and seamless to the Workers,
     - Exposing extrinsic data via serialization that maintains type-safety and ergonomics to the Workers.

## Goals

 - Keep it simple, when possible.
 - Keep it readable, when possible.
 - Keep it stateless, when possible. Avoid mutability. Gently nudge the
   developer towards product and sum types when they are modeling stateful things.
 - The ability to express complex domain relationships with a strong but gentle type system is critical.
 - Most pain in software development is felt near process boundaries. Be
   opinionated on how to make serialization as comfortable as possible while
   retaining static type safety.

## Non-goals
 - Solving heavy compute problems is a non-goal. Solve those problems at a lower level,
   and use the FFI to access those components. Generally performance optimization is treated as an unwelcome
afterthought. Favor algorithmic scaling over bit twiddling and fretting over L1 cache hits.

## Future Goals
 - Incentivize assertions. Gently penalize the lack of assertions. This becomes more interesting as dependent types are
   rolled in beyond what's available today (maybe types and conditional refinement.)
 - Pause-free execution remains a back-burner goal. (ie: Enabling Game loops, high-speed trading platforms, life support monitoring, embedded systems, etc...) However, in a future version, once language development settles down a bit, this will get more attention.
 - In-language concurrency and asynchronicity will get treatment in the future. These can currently be solved at other levels of
   the stack (hint: use OS processes.) This is not because it's not important. But, basic ergonomics of the language come first.

## Syntax

Zion looks a bit like Python, minus the colons.

```
module hello_world

def main()
	print("Hello, world!")
```

Comments use `#`.
```
def favorite_number(x int) bool
	# This is a comment
	return x == 12
```

Zion uses contains elements from ML's type system. Zion is strict, not lazy.  Memory is managed using
garbage collection.

### README TODO
- [ ] discuss namespaces.
- [ ] discuss future plans for safe dynamic dispatch. ([some thoughts exist here](https://gist.github.com/wbbradley/86cd672651cf129a91d14586523e979f))
- [ ] discuss the std library layout.

## Syntax

The Backus–Naur form for the syntax is in `syntax.ebnf`.

## Type system

Zion uses unification to match substitutable types at function callsites.
Type inference is built-in. The only automatic coercion that Zion performs is
to look for `bool_t` values for conditional statements. (TODO: revisit this
explanation...)

Types are declared as follows:

```
# Declare a structure type (aka product type, aka "struct")
type Vector2D has
	var x float
	var y float

# Note the use of the word "has" after the type name. This signifies that the
# Giraffe type "has" the succeeding "dimensions" of data associated with its
# instances.
type Giraffe has
	var name str
	var age int
	var number_of_spots int

type Gender is Male or Female

type Lion has
	var name str
	var age int
	var gender Gender

type Mouse has
	var fur_color str

# tags are how you declare a global singleton enum values.

tag Zion
tag Yellowstone
tag Yosemite

type NationalPark is
	Zion or
	Yellowstone or
	Yosemite

type Bison has
	var favorite_national_park NationalPark

# Types are not limited to being included in only one sum type, they can be
# included as subtypes of multiple supertypes. Note that Mouse is a possible
# substitutable type for either AfricanAnimal, or NorthAmericanAnimal.

type AfricanAnimal is
	Lion or
	Giraffe or
	Mouse

type NorthAmericanAnimal is
	Mouse or
	Bison
```
etc...

Some examples of standard types are:
```
type bool is
	true or
	false

# The squiggly braces are "type variables", they are not bound to the type until
# an instance of the type is created by calling its implicit constructor. If the
# "has" type contains 2 dimensions, then the generated constructor takes 2
# parameters. The question-mark indicates that the preceding type is a "maybe"
# type. This means that it can sometimes be `null`. Zion will try its best to not
# let you dereference `null` pointers.

```

The bool and vector types are declared in the standard library exactly as depicted
above. See `lib/std.zion`.

When a call to a function that takes a sum type and there still remain free type
variables, they will be substituted with the "unreachable" type (void) during
function instantiation.


### TODO

- [x] (un)signed integers
  - [x] integers as a type with parameterized number of bits and whether to use
    sign-extend or zero-extend
  - [x] promotions upon binary operators
  - [x] prevent overloading integer operations unless one side is not an integer
  - [x] deal with casting integers
- [x] implement `let` declarations
  - [ ] optimization: automatically convert parameters or `var`s that are never assigned to to `let`
- [x] change `str` to use `wchar_t` as its underlying physical character type
  - [x] use C99's `mbstowcs` and `wcstombs` to convert back and forth
  - [x] propagate usage of utf8 for `char`
- [x] 'for' syntax - based on `tests/test_list_iter.zion` pattern
- [x] vectors
- [x] Ternary operator
- [x] Logical and/or (build with ternary operator)
- [x] Type refinements for ternary / conditional expressions (it works. see [this gist](https://gist.github.com/wbbradley/6dc1ab1e12ce4312c83cd33012eb721b))
- [ ] Implement vector literal checking and code gen
- [ ] Implement vector slicing
- [ ] fully implement binary as! -> T, as -> maybe{T}
- [ ] Builtin data structures
  - [x] vectors
  - [ ] hash map - looking into hash array map tries
  - [ ] binary tree
  - [ ] avl tree
- [ ] Design/Implement tags functionality (for integration with ctags and LSP)
- [ ] decide on `with` (Python) / `using`(`dispose`) (C#) / 'defer' (Golang) style syntax for deterministic destruction
- [ ] Use DIBuilder to add line-level debugging information
- [ ] Rework debug logging to filter based on taglevels, rather than just one global level (to enable debugging particular parts more specifically)
- [ ] enable linking to variadic functions (like printf)
- [ ] write a C integer promotion compatibility test as part of test framework
