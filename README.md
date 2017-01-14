# Zion Language

“I think that it’s extraordinarily important that we in computer science keep
fun in computing. When it started out, it was an awful lot of fun. Of course,
the paying customers got shafted every now and then, and after a while we began
to take their complaints seriously.  We began to feel as if we really were
responsible for the successful, error-free perfect use of these machines. I
don’t think we are. I think we’re responsible for stretching them, setting them
off in new directions and keeping fun in the house. I hope the field of
computer science never loses its sense of fun. Above all, I hope we don’t
become missionaries. Don’t feel as if you’re Bible salesmen.  The world has too
many of those already. What you know about computing other people will learn.
Don’t feel as if the key to successful computing is only in your hands. What’s
in your hands, I think and hope, is intelligence: the ability to see the
machine as more than when you were first led up to it, that you can make it
more.”
	— Alan J. Perlis (April 1, 1922 – February 7, 1990)


Zion is an imperative exploration of strong static polymorphic (optionally
recursive) data types. Zion defaults all heap variables to an immutable state,
where stack variables are mutable.


## Tenets

 - Avoid mutability at almost all costs
 - Multi-pass compilation into a single binary
 - No preprocessor
 - Explicit versioning directly in module code
 - Simple and mandatory code formatting tools

## Notes

### LLVM

(MacOS)

`llvm-link` takes a bunch of .ir modules and links them into a single bitcode
binary. You can run a bitcode binary with `lli`.

```
llvm-link-3.7 *.ir -o output.bc
lli output.bc
```

```
brew tap homebrew/boneyard
brew install homebrew/versions/llvm37
# add /usr/local/opt/llvm/bin to your path

# add these flags to the makefile
# LDFLAGS:  -L/usr/local/opt/llvm/lib
# CPPFLAGS: -I/usr/local/opt/llvm/include
# or this to the clang++ command line
llvm-config --cxxflags --ldflags --system-libs --libs core
```

#### More info on LLVM and Compilers is available at:

 - http://llvm.org/docs/index.html
 - http://llvm.org/docs/tutorial/LangImpl3.html
 - http://llvm.org/docs/GettingStarted.html
 - http://www.ibm.com/developerworks/library/os-createcompilerllvm1/
 - http://gnuu.org/2009/09/18/writing-your-own-toy-compiler/6/
 - https://en.wikipedia.org/wiki/Control_flow_graph
 - https://en.wikipedia.org/wiki/Compiler
 - https://en.wikipedia.org/wiki/Backus%E2%80%93Naur_Form
 - https://en.wikipedia.org/wiki/Abstract_syntax_tree
 - https://en.wikipedia.org/wiki/Recursive_descent_parser

#### Tools for BNF analysis and code generation.

 - http://www.nongnu.org/bnf/

## Syntax

See `syntax.bnf`

TODO: use http://www.bottlecaps.de/rr/ui to make pretty diagrams

## Type Theory and Generics

 - http://julien.richard-foy.fr/blog/2013/02/21/be-friend-with-covariance-and-contravariance/

## Debugging

 - http://www.bergel.eu/download/papers/Berg07d-debugger.pdf

## Other ideas to explore:

 - https://en.wikipedia.org/wiki/Typed_lambda_calculus

## Syscalls

### Mac OS

 - http://www.opensource.apple.com/source/xnu/xnu-1504.3.12/bsd/kern/syscalls.master

## Linux Notes

### Ubuntu

```
sudo apt-get update
sudo apt-get install
	clang-3.6 \
	llvm-3.6-tools \
	llvm-3.6 \
	lldb-3.6-dev \
	llvm-3.6-runtime \
	libllvm3.6-dbg \
	libc++1
```

OR on Debian

```
# edit /etc/apt/sources.list with deb packages from http://llvm.org/apt/
apt-get update
apt-get install \
	git \
	clang-3.7 \
	clang-3.7-doc \
	libclang-common-3.7-dev \
	libclang-3.7-dev \
	libclang1-3.7 \
	libclang1-3.7-dbg \
	libllvm-3.7-ocaml-dev \
	libllvm3.7 \
	libllvm3.7-dbg \
	lldb-3.7 \
	llvm-3.7 \
	llvm-3.7-dev \
	llvm-3.7-doc \
	llvm-3.7-examples \
	llvm-3.7-runtime \
	clang-modernize-3.7 \
	clang-format-3.7 \
	python-clang-3.7 \
	lldb-3.7-dev \
	liblldb-3.7-dbg

cd ~
mkdir src
cd src
ssh-keygen -t rsa -b 4096 -C "latest droplet"
cat ~/.ssh/id_rsa.pub 

# copy the key to https://github.com/settings/ssh
git clone git@github.com:zionlang/zion.git
git config --global user.email "<your-email@example.com>"
git config --global user.name "Firstname Lastname"
```

OR on Standard AMI
```
sudo yum update
sudo yum install llvm-devel.x86_64 llvm-libs.x86_64 llvm.x86_64 clang.x86_64 git
cd ~/.ssh
ssh-keygen -t rsa -b 4096 -C "latest droplet"
cat ~/.ssh/id_rsa.pub 
```

### Notes on type system

New scopes contain:
- Immutable Type environments
- Variable references
- Deferred actions
  - Variable cleanup

When a call to a function that takes a sum type unifies and there are still
non-unified type terms, they will be substituted with the "unreachable" type
(void).

### Random areas to think about

 - RAII
 - Namespaces (besides modules)
 - Bubbling
   - Like defer with exceptions but yields up to enclosing scope
 - Type decorators (think tags and an algebra that integrates with Liskov's
   substitution principle but is multi-dimensional)
   - ie: const in C++ but fully extensible

There are types of abstractions beyond functions. The more practical of them
correspond to giving or relinquishing control of logic in an inward or outward
fashion. Examples of this are IoC and generators. Often it is useful to
federate out the control, and, often it is best to maintain control internally
in a single place. The core challenge of control flow abstraction of logic is
to understand which of these poles the domain problem would most benefit from.
When it seems too difficult to choose between one of the two poles, it's likely
that we don't yet understand the problem space fully enough.

### TODO

- Memory management scheme options
  - ref counting since all data is immutable, there can be no cycles
  - GC 1 - wrap creation and scope termination with shadow stack operations
  - Rust style owning/borrowing semantics
  - C style explicit allocation/free
- Rework debug logging to filter based on taglevels, rather than just one global
  level (to enable debugging particular parts more specifically)
- Include runtime behavior in test suite (not just compilation errors)
- Ternary operator
  - Logical and/or (build with ternary operator)
- fully implement binary as! -> T, as -> maybe{T}
- figure out how to make tuples' runtime types typecheck consistently against
  rtti for other data ctor types. probably have to put up some guardrails somehow
- Use DIBuilder to add line-level debugging information
- Builtin persistent data structures
  - vector
  - hash map
  - binary tree
  - avl tree
- Plumb "desired type" through resolve_instantiation in order to influence
  literals into being native by default, unless boxed by a "desired type"
  - This quickly leads to complexity around the following scenario:
	  def foo(x int)
		  pass
	  def foo(x __int__)
		  pass
	  foo(4)
    where the '4' literal should get the type `int lazy-or __int__`, which then
	allows it to resolve against both versions of `foo`. This would be an
	ambiguity error. the `lazy-or` type is a lazily bound type that could cause
	a bound_var to be automatically boxed at a callsite or assignment.

