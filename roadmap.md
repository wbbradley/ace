# Zion Roadmap

## Language/Compiler (zion)

- [x] global variable initialization (v0.3)
- [ ] error on unused imports (v0.4)
- [ ] macro language around data structures for deriving support (v0.5)
- [ ] property mptc auto-class and instance generation

### Performance

- [ ] escape analysis (v1.1)
  - [ ] adjust memory model to utilize double-dereference (to allow for pointer rewriting)
  - [ ] escape stack vars to heap vars
- [ ] move semantics and other [substructural type systems](https://en.wikipedia.org/wiki/Substructural_type_system) (v1.2)

## Standard Library (lib/std)

- [ ] option parsing - update lib/argparse.zion

## Ecosystem
- [ ] a package management system (v0.8)
- [ ] incorporate versioning into module resolution and type system (v0.9)
- [ ] onboarding page - tutorial, etc...
