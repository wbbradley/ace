# Zion Language

[![Build Status](https://travis-ci.org/zionlang/zion.svg?branch=master)](https://travis-ci.org/zionlang/zion)

Zion is a programming language. It is an imperative exploration of strong static
polymorphic (optionally recursive) data types. Zion defaults all heap data to an
immutable state, where variable names can be repointed to other values. This is
similar to how Python treats its basic types, `int`, `str`, `float`, `bool`, but
Zion extends this treatment to all types.

## Tenets

 - Keep it simple, when possible
 - Keep it readable, when possible
 - Keep it stateless, when possible. Avoid mutability. Gently nudge the
   developer towards product and sum types when they are modeling stateful things.
 - The ability to express complex domain relationships with static types is
   critical
 - Deterministic destruction is good
 - Serialization and marshalling in general must be foremost in both the syntax
   and the runtime
 - Compile down to a copyable binary
 - No preprocessor
 - In-proc parallelism is a non-goal, scaling is intended to happen at a higher
   level
 - Heavy compute problems are a non-goal, solve those problems at a lower level,
   and use the FFI, or RPC to access those libraries/components. Generally
   performance optimization is treated as an unwelcome afterthought. Favor
   algorithms and horizontal scaling over bit twiddling.

## Notes

Zion looks a bit like Python:

```
module hello_world

def main()
	print("Hello, world!")
```

Here's an old friend:
```
def fib(n int) int
	if n <= 2
		return 1

	return fib(n-1) + fib(n-2)
```

Zion contains elements from ML's type system. Zion is strict, not lazy. It is
compiled down to machine code using LLVM. Memory is managed using an extremely
simplistic reference counting scheme. All data is immutable, therefore cycles
are impossible, which eliminates the need for generational mark and sweep or
"stop the world" garbage collection.

### TODO
- [ ] discuss function overrides.
- [ ] discuss callsite context types.
- [ ] discuss future plans for safe dynamic dispatch.
- [ ] discuss the std library layout.

## Syntax

The Backus–Naur form for the syntax is in `syntax.ebnf`.

## Type system

Zion uses unification to match substitutable types at function callsites.
Type inference is built-in.

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
# type. This means that it can sometimes be nil. Zion will not let you
# dereference nil pointers.

type list{T} has
	var value T
	var next list{T}?
```

The bool and list types are declared in the standard library exactly as depicted
above. See `lib/std.zion`.

When a call to a function that takes a sum type unifies and there are still
non-unified type terms, they will be substituted with the "unreachable" type
(void).


### TODO

- [x] Memory management scheme options
 - [x] ref counting since all data is immutable, there can be no cycles
 - [ ] C style explicit allocation/free - this requires some explicit memory management affordances, such as `with` syntax to get deterministic destruction.
- [ ] Rework debug logging to filter based on taglevels, rather than just one global level (to enable debugging particular parts more specifically)
[x] Include runtime behavior in test suite (not just compilation errors)
[x] Ternary operator
  - [ ] Logical and/or (build with ternary operator)
- [ ] fully implement binary as! -> T, as -> maybe{T}
- [ ] figure out how to make tuples' runtime types typecheck consistently against rtti for other data ctor types. probably have to put up some guardrails somehow
- [ ] Use DIBuilder to add line-level debugging information
- [ ] Builtin persistent data structures
  - [ ] vector
  - [ ] hash map - looking into hash array map tries
  - [ ] binary tree
  - [ ] avl tree
