<!--- -*- gfm -*- -->

# unodb

[![Github Actions Build Status](https://github.com/laurynas-biveinis/unodb/workflows/build/badge.svg)](https://github.com/laurynas-biveinis/unodb/actions?query=workflow%3Abuild)
[![codecov](https://codecov.io/gh/laurynas-biveinis/unodb/branch/master/graph/badge.svg)](https://codecov.io/gh/laurynas-biveinis/unodb)
[![GitHub
Super-Linter](https://github.com/laurynas-biveinis/unodb/workflows/Super-Linter/badge.svg)](https://github.com/marketplace/actions/super-linter)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=laurynas-biveinis_unodb&metric=alert_status)](https://sonarcloud.io/dashboard?id=laurynas-biveinis_unodb)

## Introduction

UnoDB is an implementation of Adaptive Radix Tree in two flavors – a regular one
and Optimistic Lock Coupling-based concurrent one with Quiescent State Based
Reclamation (QSBR). It is done as my playground for various C++ tools and ideas,
and I am trying to describe some of the things I learned at my [blog](https://of-code.blogspot.com/search/label/art).

## Requirements

The source code is C++17, using SSE4.1 intrinsics (Nehalem and higher) or AVX
in the case of MSVC. This is in contrast to the original ART paper needing SSE2
only.

Note: since this is my personal project, it only supports GCC 10, 11, LLVM 11 to
13, XCode 13.2, and MSVC 2022 (17.0) compilers. Drop me a note if you want to
try this and need a lower supported compiler version.

## Usage

All the declarations live in the `unodb` namespace, which is omitted in the
following.

The only currently supported key type is `std::uint64_t`, aliased as `key`.
However, adding new key types should be relatively easy by instantiating
`art_key` type with the desired key type and specializing
`art_key::make_binary_comparable` in accordance with the ART paper.

Values are treated opaquely. For `unodb::db`, they are passed as non-owning
objects of `value_view`, which is `gsl::span<std::byte>`, and insertion copies
them internally. The same applies for `get`: a non-owning `value_view` object is
returned. For `unodb::olc_db`, `get` returns a `qsbr_value_view`, which is a
`span` that is guaranteed to stay valid until the next time the current thread
passes through a quiescent state.

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

Any macros starting with `UNODB_DETAIL_` are internal and should not be used.

## Dependencies

* CMake, at least 3.12
* Boost library. Currently tested with versions 1.74 and 1.75.
* Guidelines Support Library for gsl::span, bundled as a git submodule.
* Google Test for tests, bundled as a git submodule.
* Google Benchmark for microbenchmarks, bundled, as a git submodule.
* [DeepState][deepstate] for fuzzing tests, bundled as a git submodule.
* (optional) clang-format
* (optional) lcov
* (optional) clang-tidy
* (optional) clangd
* (optional) cppcheck 2.5
* (optional) cpplint
* (optional) include-what-you-use
* (optional) libfuzzer

## Development

Source code is formatted with [Google C++ style][gc++style]. Automatic code
formatting is configured through git  clean/fuzz filters. To enable it, do `git
config --local include.path ../.gitconfig`. If for any reason you need to
disable it temporarily, do `git config  --local --unset include.path`

To make compiler warnings fatal, add `-DFATAL_WARNINGS=ON` CMake option.

clang-tidy, cppcheck, and cpplint will be invoked automatically during build if
found. Currently the diagnostic level for them as well as for compiler warnings
is set very high, and can be relaxed, especially for clang-tidy, as need arises.

To enable AddressSanitizer and LeakSanitizer (the latter if available), add
`-DSANITIZE_ADDRESS=ON` CMake option. It is incompatible with
`-DSANITIZE_THREAD=ON`.

To enable ThreadSanitizer, add `-DSANITIZE_THREAD=ON` CMake option. It is
incompatible with `-DSANITIZE_ADDRESS=ON`. It is also incompatible with
libfuzzer, and will disable its support if specified. Not available under MSVC.

To enable UndefinedBehaviorSanitizer, add `-DSANITIZE_UB=ON` CMake option. It is
compatible with both `-DSANITIZE_ADDRESS=ON` and `-DSANITIZE_THREAD=ON` options,
although some [false positives][sanitizer-combination-bug] might occur. Not
available under MSVC.

To enable GCC or MSVC compiler static analysis, add `-DSTATIC_ANALYSIS=ON` CMake
option. For LLVM static analysis, no special CMake option is needed, and you
have to prepend `scan-build` to `make` instead.

To invoke include-what-you-use, add `-DIWYU=ON` CMake option. It will take
effect if CMake configures to build project with clang.

To enable inconclusive cppcheck diagnostics, add `-DCPPCHECK_AGGRESSIVE=ON`
CMake option. These diagnostics will not fail a build.

To generate coverage reports on tests, fuzzers excluded, using lcov, add
`-DCOVERAGE=ON` CMake option.

Google Test and DeepState are used for testing. There will be no unit tests for
each private implementation class. For DeepState, both LLVM libfuzzer and
built-in fuzzer are supported.

## Fuzzing

There are fuzzer tests for `unodb::db` and QSBR components in the
`fuzz_deepstate` subdirectory. The tests use DeepState with either brute force
or libfuzzer-based backend. Not all platforms and configurations support them,
i.e. MSVC build completely skips them, and libfuzzer-based tests are skipped if
ThreadSanitizer is enabled, or if building non-XCode clang release
configuration.

There are several Make targets for fuzzing. For time-based brute-force fuzzing
of all components, use on of `deepstate_2s`, `deepstate_1m`, `deepstate_20m`,
and `deepstate_8h`. Individual fuzzers can be used by inserting `art` or `qsbr`,
i.e. `deepstate_qsbr_20m` or `deepstate_art_8h`. Running fuzzer under Valgrind
is available through `valgrind_deepstate` for everything or
`valgrind_{art|qsbr}_deepstate` for individual fuzzers.

Fuzzers that use libfuzzer mirror the above by adding `_lf` before the time
suffix, i.e. `deepstate_lf_8h`, `deepstate_qsbr_lf_20m`,
`valgrind_deepstate_lf`, and so on.

## Related projects

[art_map](https://github.com/justinasvd/art_map) is a C++14 template library
providing `std::`-like interface over ART. It shares some code with UnoDB.

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

[sanitizer-combination-bug]: https://github.com/google/sanitizers/issues/1106

[gc++style]: https://google.github.io/styleguide/cppguide.html
"Google C++ Style Guide"

[deepstate]: https://github.com/trailofbits/deepstate "DeepState on GitHub"

[seqlock]: https://en.wikipedia.org/wiki/Seqlock
