<!--- -*- gfm -*- -->

# unodb

[![Github Actions Build Status](https://github.com/laurynas-biveinis/unodb/workflows/build/badge.svg)](https://github.com/laurynas-biveinis/unodb/actions?query=workflow%3Abuild)
[![codecov](https://codecov.io/gh/laurynas-biveinis/unodb/branch/master/graph/badge.svg)](https://codecov.io/gh/laurynas-biveinis/unodb)
[![GitHub
Super-Linter](https://github.com/laurynas-biveinis/unodb/workflows/Super-Linter/badge.svg)](https://github.com/marketplace/actions/super-linter)
[![Travis-CI Build
Status](https://travis-ci.org/laurynas-biveinis/unodb.svg?branch=master)](https://travis-ci.org/laurynas-biveinis/unodb)

## Introduction

Unodb is a adaptive radix tree implementation, done as my playground for various
C++ tools and ideas. I am trying to describe some of the things I learned at my [blog](https://of-code.blogspot.com/search/label/art).

## Requirements

The code uses SSE4.1 intrinsics (Nehalem and higher). This is in contrast to the
original ART paper needing SSE2 only.

Note: since this is my personal project, it only supports GCC 10 and LLVM 11
compilers. Drop me a note if you want to try this and need a lower supported
compiler version.

## Usage

All the declarations live in the `unodb` namespace, which is omitted in the
following.

The only currently supported key type is `std::uint64_t`, aliased as `key`.
However, adding new key types should be relatively easy by instantiating
`art_key` type with the desired key type and specializing
`art_key::make_binary_comparable` in accordance with the ART paper.

Values are treated opaquely. They are passed as non-owning objects of
`value_view`, which is `gsl::span<std::byte>`, and insertion copies them
internally. The same applies for `get`: a non-owning `value_view` object is
returned. How long would it remain valid depends on the ART concurrency flavor.

All ART classes implement the same API:

* constructor.
* `get(key k)`, returning `get_result`, which is `std::optional<value_view>`.
* `bool insert(key k, value_view v)`, returning whether insert was
  successful (i.e. the key was not already present).
* `bool remove(key k)`, returning whether delete was successful (i.e. the
  key was found in the tree).
* `clear`, making the tree empty. For `olc_db`, it is not a concurrent operation
  and must be called from a single-threaded context.
* `bool empty()`, returning whether the tree is empty.
* `void dump(std::ostream &)`, dumping the tree representation into output
  stream.
* Several getters for assorted tree info, such as current memory use, and
  counters of various internal tree  operations (i.e. number of times Node4 grew
  to Node16, key prefix was split, etc - check the source code).

The are three ART classes available:

* `db`: unsychronized ART tree, to be used in single-thread context or with
  external synchronization.
* `mutex_db`: single global mutex-synchronized ART tree.
* `olc_db`: a concurrent ART tree, implementing Optimistic Lock Coupling as
  described by Leis et al. in the "The ART of Practical Synchronization" paper;
  the nodes are versioned, the writers lock per-node so-called optimistic lock,
  the readers don't lock anything, but check node versions and restart if they
  change. The optimistic lock concept seems to be nearly identical to that of
  [sequential locks][seqlock] as used in the Linux kernel, with the addition of
  "obsolete" state. The lock implementation uses seqlock memory model
  implementation as described by Boehm in "Can seqlocks get along with
  programming language memory models?" 2012 paper. The OLC ART implementation
  necessitates a memory reclamation scheme as required by lock-free data
  structures (even though `olc_db` is not lock-free in the general sense, the
  readers do not take locks), and for that a Quiescent State Based Reclamation
  (QSBR) was chosen.

## Dependencies

* git
* a C++17 compiler, currently tested with clang 11, XCode clang 12 and GCC 10.2.
* CMake, at least 3.12
* Guidelines Support Library for gsl::span, imported as a git submodule.
* Boost library. Currently tested with versions 1.74 and 1.75.
* clang-format 9.0
* Google Test for tests, imported as a git submodule.
* (optional) lcov
* (optional) clang-tidy
* (optional) cppcheck
* (optional) cpplint
* (optional) include-what-you-use
* (optional) [DeepState][deepstate] for fuzzing, currently working on macOS only
* (optional) Google Benchmark for microbenchmarks.

## Development

Source code is formatted with [Google C++ style][gc++style]. Automatic code
formatting is configured through git  clean/fuzz filters. To enable it, do `git
config --local include.path ../.gitconfig`. If for any reason you need to
disable it temporarily, do `git config  --local --unset include.path`

clang-tidy, cppcheck, and cpplint will be invoked automatically during build if
found. Currently the diagnostic level for them as well as for compiler warnings
is set very high, and can be relaxed, especially for clang-tidy, as need arises.

To enable AddressSanitizer and LeakSanitizers, add `-DSANITIZE_ADDRESS=ON` CMake
option. It is incompatible with `-DSANITIZE_THREAD=ON`.

To enable ThreadSanitizer, add `-DSANITIZE_THREAD=ON` CMake option. It is
incompatible with `-DSANITIZE_ADDRESS=ON`

To enable UndefinedBehaviorSanitizer, add `-DSANITIZE_UB=ON` CMake option. It is
compatible with both `-DSANITIZE_ADDRESS=ON` and `-DSANITIZE_THREAD=ON` options,
although some [false positives][sanitizer-combination-bug] might occur. Enabling
it with GCC, where `std::pmr` from libstdc++ is used instead of `boost::pmr`,
will result in UBSan false positives due to [this][libstdc++ub].

To enable GCC 10+ compiler static analysis, add `-DSTATIC_ANALYSIS=ON` CMake
option. For LLVM static analysis, no special CMake option is needed, and you
have to prepend `scan-build` to `make` instead.

To invoke include-what-you-use, add `-DIWYU=ON` CMake option. It will take
effect if CMake configures to build project with clang.

To enable inconclusive cppcheck diagnostics, add `-DCPPCHECK_AGGRESSIVE=ON`
CMake option.

To generate coverage reports on tests, fuzzers excluded, using lcov, add
`-DCOVERAGE=ON` CMake option.

Google Test and DeepState are used for testing. There will be no unit tests for
each private implementation class. For DeepState, both LLVM libfuzzer and
built-in fuzzer are supported.

## Literature

*ART Trie*: V. Leis, A. Kemper and T. Neumann, "The adaptive radix tree: ARTful
indexing for main-memory databases," 2013 29th IEEE International Conference on
Data Engineering (ICDE 2013)(ICDE), Brisbane, QLD, 2013, pp. 38-49.
doi:10.1109/ICDE.2013.6544812

*ART Sync*: V. Leis, F. Schneiber, A. Kemper and T. Neumann, "The ART of
Practical Synchronization," 2016 Proceedings of the 12th International Workshop
on Data Management on New Hardware (DaMoN), pages 3:1--3:8, 2016.

*qsbr*: P. E. McKenney, J. D. Slingwine, "Read-copy update: using execution
history to solve concurrency problems," Parallel and Distributed Computing and
Systems, 1998, pages 509--518.

*seqlock sync*: H-J. Boehm, "Can seqlocks get along with programming language
memory models?," Proceedings of the 2012 ACM SIGPLAN Workshop on Memory Systems
Performance and Correctness, June 2012, pages 12--21, 2012.

[boostub1]: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80963

[boostub2]: https://bugs.llvm.org/show_bug.cgi?id=39191

[sanitizer-combination-bug]: https://github.com/google/sanitizers/issues/1106

[libstdc++ub]: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90442

[gc++style]: https://google.github.io/styleguide/cppguide.html
"Google C++ Style Guide"

[deepstate]: https://github.com/trailofbits/deepstate "DeepState on GitHub"

[seqlock]: https://en.wikipedia.org/wiki/Seqlock
