# Zion Roadmap

## Language/Compiler (zion)

- [x] (v0.3) global variable initialization
- [ ] macro language around data structures for
  - [ ] property mptc auto-class and instance generation
  - [ ] serialization or type decl annotations
  - [ ] automatic derived typeclass instance implementations
- [ ] error on unused imports
- [ ] allow nested modules (ie: net/http)
- [ ] design/implement some form of [functional dependencies](https://github.com/zionlang/reference-docs/blob/master/docs/2000-jones-functional-dependencies.pdf) for mptc sanity
- [ ] unused variable check (variables that only appear on lhs)

## Standard Library (lib/std)

- [ ] option parsing - update lib/argparse.zion
- [ ] net/http client
- [ ] net/http server
- [ ] HTML parser

## Ecosystem
- [ ] language reference documentation
- [ ] example code documentation
- [ ] tutorial documentation
- [ ] finalize name (Zion is a codename)
- [ ] homebrew bottling releases
- [ ] a debian package
- [ ] a package management system (v0.8)
- [ ] incorporate versioning into module resolution and type system (v0.9)
- [ ] onboarding page - tutorial, etc...

## Tooling
- [ ] zion-format tool
- [ ] `compile` should build binary, rename existing `compile` phase to `check`

### Performance

- [ ] escape analysis (v1.1)
  - [ ] adjust memory model to utilize double-dereference (to allow for pointer rewriting)
  - [ ] escape stack vars to heap vars
- [ ] move semantics and other [substructural type systems](https://en.wikipedia.org/wiki/Substructural_type_system) (v1.2)
- [ ] improve compilation speeds
- [ ] benchmark JSON parsing speed
- [ ] compiler: optimize coverage analysis

