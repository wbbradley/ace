# Cider Roadmap

## Language/Compiler (cider)

- [x] (v0.3) global variable initialization
- [ ] do notation
- [ ] allow regular assignment in instance declarations (let f = not . g)
- [ ] macro language around data structures for
  - [ ] property mptc auto-class and instance generation
  - [ ] serialization or type decl annotations
  - [ ] automatic derived typeclass instance implementations (See https://www.haskell.org/onlinereport/haskell2010/haskellch11.html)
    - [x] deriving Eq
    - [ ] deriving Str
    - [ ] deriving Ord
- [ ] error on unused imports
- [ ] allow nested modules (ie: net/http)
- [ ] design/implement some form of [functional dependencies](https://github.com/ciderlang/reference-docs/blob/master/docs/2000-jones-functional-dependencies.pdf) for mptc sanity
- [ ] unused variable check (variables that only appear on lhs)
- [ ] higher-kinded type functions to allow for pulling types from other types for the purpose of mapping between types (for example to_with :: Either a b -> WithElseResource resource error)

## Standard Library (lib/std)

- [ ] gain consistency around error handling in lib/sys, etc. (Choose between Either Errno () and WithElseResource where it makes sense)
- [ ] option parsing - update lib/argparse.cider
- [ ] net/http client
- [ ] net/http server
- [ ] HTML parser

## Ecosystem
- [ ] language reference documentation
- [ ] example code documentation
- [ ] tutorial documentation
- [ ] finalize name (Cider is a codename)
- [ ] homebrew bottling releases
- [ ] a debian package
- [ ] a package management system (v0.8)
- [ ] incorporate versioning into module resolution and type system (v0.9)
- [ ] onboarding page - tutorial, etc...

## Tooling
- [ ] cider-format tool
- [ ] `compile` should build binary, rename existing `compile` phase to `check`

### Performance

- [ ] escape analysis (v1.1)
  - [ ] adjust memory model to utilize double-dereference (to allow for pointer rewriting)
  - [ ] escape stack vars to heap vars
- [ ] move semantics and other [substructural type systems](https://en.wikipedia.org/wiki/Substructural_type_system) (v1.2)
- [ ] improve compilation speeds
- [ ] benchmark JSON parsing speed
- [ ] compiler: optimize coverage analysis

