# Development of Ace

## Machine setup

So far all development of Ace has taken place on MacOS and Linux. The most
well-supported path is to develop on MacOS at the moment. However, all tests in
GitHub Actions are running in a Docker debian:latest container. Consult
[Dockerfile](Dockerfile) for more info on that test setup.

Given the above, we want development to be comfortable on any of the major
platforms, so please try to get it working, and we will do our best to unblock
you!

### Tools you'll need

For the official list of compile-time dependencies, consult
[install-deps.sh](install-deps.sh).

You will need Docker for the supported Linux test path.

#### Plugins

* If you use vim, take a look at
  [vim-ace](https://github.com/wbbradley/vim-ace).


## Development workflow

### Building

```
make clean
           # then choose debug or release
make       # to build the release build
make debug # to build the debug build
```

NB: the make clean step is mandatory when you are switching between debug and
release, but after you've done a make clean and you are jamming in either
configuration you don't need to run make clean.

The [Makefile](Makefile) is primarily driving CMake and running installation to
`/usr/local`. There is a `make uninstall` step which can be useful at times.

### Running tests

To run tests on your host machine:

```
make clean
make test
```

To run Linux tests:

```
./docker-run.sh
```

Tests are run by a horrible Bash script called [run-tests.sh](tests/run-tests.sh). The
one nice thing about this script is that it will attempt to parallelize the
tests by utilizing a semaphore made out of a `mkfifo` stream. Your perf mileage
may vary with that depending on the strength of your machine.
