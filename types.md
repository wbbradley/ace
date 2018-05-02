# Type Reference Expression Syntax

Operator                                            | Description                  | Notes
-------------                                       | ---------------------------- | -------------
`a`, `42`, `"Hello world."`                         | Type literals. Symbols, integers, and strings. | Smallest units of type system. Form the basis of the nominal type system. Highest precedence.
`f` **`(`** `g` `x` **`)`**<br/>`f` **`$`** `g` `x` | Precedence operator (parentheses or `$`) | Resets precedence rules
**`[`** `a` **`]`**                                 | `vector.Vector{a}` reader macro | 
**`{`** `a` **`:`** `b` **`}`**<br/>**`{`** `a` **`,`** `b` **`}`**                     | `map.Map{a, b}` or `tuple{a, b}` reader macros, respectively | Precedence rules are reset between braces and colon.
**`any`** `T`                                       | Type variable reference | Names a free type variable in the context of the entire type expression. Ensures that the symbol does not reference anything in the environment (until some substitution occurs perhaps). Can be coupled to other instances of itself by name.
**`def`** **`(`** *param-list* **`)`** *return-type*<br/>**`def`** *name* **`{`** *type-constraints* **`}`** **`(`** *param-list* **`)`** *return-type* | Function type | *name* is only valid in function declarations or in `exists` invocations 
**`lambda`** `x` `body`                             | Lambda expression | Replaces occurrences of `x` in `body` with whatever was applied to it. `(\x.f x) y` `=>` `f y`. `x` becomes an implicit Type variable within `body`. The body of the abstraction extends as far right as possible.
`a` **`?`**<br/>`map{int, str?}`<br/>`ShoeSize{EnglishUnits}?` | Optional type modifier (Maybe types)                  | Right-to-left
`x y z` which is the same as `(x y) z` as well as `x{y} z` | Standard term application    | Left-to-right
**`*?`** `a`<br/>`*?foreign_struct_t`               | Optional native pointer (may be a `null` pointer)   | Right-to-left
**`*`** `a`<br/>`*FILE`                             | Native pointer. Treated as non-`null`.  | Right-to-left
**`&`** `a`<br/>`&int`                              | Native reference. Implies the presence of a stack or global address.  | Reference types cannot be passed in to function, and cannot be returned from functions.
`a` **`===`** `b`<br/>                              | Weak Normal Form Congruence (aka equality but not really) |
`a` **`and`** `b`<br/>`gc a` **`and`** `b not c{d} e` which is the same as `and{gc{a}, b{not{c{d, e}}}}`      | Logical AND  |
`a` **`or`** `b`<br/>`gc a` **`or`** `b not c{d} e` which is the same as `or{gc{a}, b{not{c{d, e}}}}`      | Logical OR    | Lowest precedence.

# Type Statements Syntax

## Type Definitions
Type definitions enable reification of a symbol into the related environment both statically and at runtime. Statically they allow for nominal unification.
The fully-bound and fully-qualified name of a type definition gives an `atomize`d value which assigns a runtime identity for runtime type-checking purposes.

Type declarations. Usually occur at module scope.
* **`type`** `X` [ `struct` \| `has` \| `is` \| `=` *type-reference* ]

### Structural Types

Keywords | Description | Notes
--- | --- | ---
**`struct`** ...                                    | native product type with named dimensions | Structural type that does not impact unification
**`has`** ...                                       | managed product type with named dimensions | Structural type that does not impact unification
**`is`** `a` **`or`** `b` ...                       | managed sum type | Nominal type that impacts unification. `is` will consume all chained `or` options into itself.

Tag declarations. Also usually occur at module scope.
* **`tag`** `Y`

## Runtime type information

Encoding RTTI for pattern matching with infinite types

Example

```
tag Foo
tag Bar
tag Baz
type Node = Foo or Bar or Baz or vector.Vector Node

These are all "Node" substitutable:

Foo
[Node]
[[Node]]
[[[[Bar or Baz]]] or Foo or Bar]

A type defines an function that will parse the type signature and return true or
false. In future incantations, it could also fill out inner parts.


Foo => 1
Bar => 2
Baz => 3
Node => 4
Vector => 5

Concrete type ids are written as encoded tuples.

[Node]       -> (5,4)
[[Node]]     -> (5,(5,4))
[Bar or Foo or [Node]] -> (5,(or (2,1,(5,4))))

Build a table of relations:

1: 0
2: 0
3: 0
4: (or (1, 2, 3, (5, 4)))
5: 0
To match [Bar or Baz] : (5, (or (2, 3))

Against [Bar] : (5, 2)

Match 5 
Merge-match input against 2,3
	Accept tuple or scalar
	if scalar, search for value in match-list
	if tuple, search for each value in match-list, in order

To match [Node] : (5, (or (1, 2, 3, #[Node])))
Against [[

```


