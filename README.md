<!--- -*- gfm -*- -->

# unodb

[![Github Actions Build
Status](https://github.com/laurynas-biveinis/unodb/workflows/build/badge.svg)](https://github.com/laurynas-biveinis/unodb/actions?query=workflow%3Abuild)
[![laurynas-biveinis](https://circleci.com/gh/laurynas-biveinis/unodb.svg?style=svg)](https://app.circleci.com/pipelines/github/laurynas-biveinis/unodb)
[![codecov](https://codecov.io/gh/laurynas-biveinis/unodb/branch/master/graph/badge.svg)](https://codecov.io/gh/laurynas-biveinis/unodb)
[![GitHub
Super-Linter](https://github.com/laurynas-biveinis/unodb/workflows/Super-Linter/badge.svg)](https://github.com/marketplace/actions/super-linter)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=laurynas-biveinis_unodb&metric=alert_status)](https://sonarcloud.io/dashboard?id=laurynas-biveinis_unodb)

## Introduction

UnoDB is a library that implements of Adaptive Radix Tree (ART) data structure,
designed for efficient indexing in main-memory databases. The ART is a
trie-based data structure that dynamically adapts to the distribution of keys,
ensuring good search performance and memory usage. UnoDB offers two variants of
ART: a regular one and a concurrent one based on Optimistic Lock Coupling (OLC)
with Quiescent State Based Reclamation (QSBR).

This library serves as a playground for experimenting with various C++ tools and
ideas. I am describing some of the things I learned at my [blog](https://of-code.blogspot.com/search/label/art).

## Requirements

UnoDB' source code is written in C++17, and relies on the following
platform-specific features:

* On Intel platforms, it requires SSE4.1 intrinsics (Nehalem and higher), AVX
  for MSVC builds, and optional AVX2 support, when available. This differs from
  the original ART paper, which only required SSE2.
* On ARM, it uses NEON intrinsics.

Please note that this personal project only supports the following compilers:
GCC 10 and later, LLVM 11 and later, XCode 13.2, and MSVC 2022 compilers. If you
require support for an earlier compiler version, feel free to drop me a note.

Some platform-specific notes:

### Amazon Linux 2023

```bash
sudo dnf install git gcc g++ cmake boost-devel
# Optional, if you want to use Boost.Stacktrace:
git clone https://github.com/ianlancetaylor/libbacktrace
(cd libbacktrace && ./configure && make && sudo make install)
# Build as usual
```

## Usage

All the declarations live in the `unodb` namespace, which is omitted in the
descriptions below.

The only currently supported key type is `std::uint64_t`, aliased as `key`. To
add new key types, instantiate `art_key` type with the desired type, and
specialize `art_key::make_binary_comparable` according to the ART paper.

Values are treated opaquely. For `unodb::db`, they are passed as non-owning
objects of `value_view` (a `gsl::span<std::byte>`), and insertion copies them
internally. The same applies for `get`, which returns a non-owning `value_view`.
For `unodb::olc_db`, `get` returns a `qsbr_value_view`, a `span` guaranteed to
remain valid until the current thread passes through a quiescent state.

All ART classes share the same API:

* constructor.
* `get(key k)` returns `get_result` (a `std::optional<value_view>`).
* `bool insert(key k, value_view v)` returns whether the insert was successful
  (i.e. the key was not already present).
* `bool remove(key k)` returns whether delete was successful (i.e. the key was
  found in the tree).
* `clear()` empties the tree. For `olc_db`, it must be called from a
  single-threaded context.
* `bool empty()` returns whether the tree is empty.
* `void dump(std::ostream &)` outputs the tree representation.
* Several getters provide tree info, such as current memory use, and internal
  operation counters (e.g. number of times Node4 grew to Node16, key prefix was
  split, etc - see the source code for details).

Three ART classes available:

* `db`: unsychronized ART tree, for single-thread contexts or with
  external synchronization
* `mutex_db`: ART tree with single global mutex synchronization
* `olc_db`: a concurrent ART tree, implementing Optimistic Lock Coupling as
  described in "The ART of Practical Synchronization" paper by Leis et al.;
  nodes are versioned, writers lock per-node optimistic locks, readers don't
  lock but check node versions and restart if they change.

Do not use macros starting with `UNODB_DETAIL_` or declarations in
`unodb::detail` and `unob::test` namespaces as they are internal and may change
at any time.

## Technical Details

### Sequential Lock

The optimistic lock concept seems to be nearly identical to that of [sequential
locks][seqlock] as used in the Linux kernel, with the addition of "obsolete"
state. The lock implementation uses the seqlock memory model implementation as
described by Boehm in "Can seqlocks get along with programming language memory
models?" 2012 paper.

### Quiescent State-Based Reclamation (QSBR)

The OLC ART implementation necessitates a memory reclamation scheme as required
by lock-free data structures (even though `olc_db` is not lock-free in the
general sense, the readers do not take locks), and for that a Quiescent State
Based Reclamation (QSBR) was chosen. In QSBR, each thread periodically announces
that it is not using any pointers to a shared data structures. After all the
threads have done so, a new epoch starts, and the memory reclamation requests
from two epochs ago are executed. To participate in QSBR, a thread must an
instance of `unodb::qsbr_thread`, which derives from `std::thread`. A thread may
temporarily or permanently stop its participation in QSBR by calling
`unodb::this_thread().qsbr_pause()` and resume by calling
`unodb::this_thread().qsbr_resume()`.

The registered threads must periodically signal their quiescent states. They can
do this by using the `unodb::quiescent_state_on_scope_exit` scope guard, which
automatically reports the quiescent state when the scope is exited.

## Dependencies

* CMake, at least 3.12
* Boost library.
* Guidelines Support Library for gsl::span, bundled as a git submodule.
* Google Test for tests, bundled as a git submodule.
* Google Benchmark for microbenchmarks, bundled, as a git submodule.
* [DeepState][deepstate] for fuzzing tests, bundled as a git submodule.
* (optional) clang-format
* (optional) lcov
* (optional) clang-tidy
* (optional) clangd
* (optional) cppcheck
* (optional) cpplint
* (optional) include-what-you-use
* (optional) libfuzzer

## Development

Source code is formatted with [Google C++ style][gc++style]. Automatic code
formatting is configured through git  clean/fuzz filters. To enable this
feature, do `git config --local include.path ../.gitconfig`. If you need to
temporarily disable it, run `git config  --local --unset include.path`.

When building this project independently and not as part of another project, add
`-DSTANDALONE=ON` CMake option. It will enable extra global debug checks that
require entire programs to be compiled with them. Currently, this consists of
the libstdc++ debug mode.

To enable maintainer diagnostics, add `-DMAINTAINER_MODE=ON` CMake option. This
makes compilation and `include-what-you-use` warnings fatal.

clang-tidy, cppcheck, and cpplint will be invoked automatically during the build
if found. The current diagnostic level for them, as well as for compiler
warnings, is set very high and can be relaxed if needed.

To disable AVX2 intrinsics to use SSE4.1/AVX only, add `-DWITH_AVX2=OFF`.

To enable AddressSanitizer and LeakSanitizer (the latter if available), add
`-DSANITIZE_ADDRESS=ON` CMake option. It is incompatible with the
`-DSANITIZE_THREAD=ON` option.

To enable ThreadSanitizer, add `-DSANITIZE_THREAD=ON` CMake option. It is
incompatible with the `-DSANITIZE_ADDRESS=ON` option and will disable libfuzzer
support if specified. Not available under MSVC.

To enable UndefinedBehaviorSanitizer, add the `-DSANITIZE_UB=ON` CMake option.
It is compatible with both `-DSANITIZE_ADDRESS=ON` and `-DSANITIZE_THREAD=ON`
options, although some [false positives][sanitizer-combination-bug] might occur.
Not available under MSVC.

To enable GCC or MSVC compiler static analysis, add the `-DSTATIC_ANALYSIS=ON`
CMake option. For LLVM static analysis, no special CMake option is needed;
instead prepend `scan-build` to `make`.

To invoke include-what-you-use without enabling the whole of maintainer mode,
add the `-DIWYU=ON` CMake option. It will take effect if CMake configures to
build project with clang.

To enable inconclusive cppcheck diagnostics, add the `-DCPPCHECK_AGGRESSIVE=ON`
CMake option. These diagnostics will not fail a build.

To generate coverage reports on tests, excluding fuzzers, using lcov, add the
`-DCOVERAGE=ON` CMake option.

Google Test and DeepState are used for testing. There will be no unit tests for
each private implementation class. For DeepState, both LLVM libfuzzer and
built-in fuzzer are supported.

## Fuzzing

 Fuzzer tests for ART and QSBR components are located in the `fuzz_deepstate`
subdirectory. The tests use DeepState with either a brute force or
libfuzzer-based backend. However, not all platforms and configurations support
them. For isntance, MSVC builds completely skip them, and libfuzzer-based tests
are skipped if ThreadSanitizer is enabled or if building non-XCode clang release
configuration.

Several Make targets are available for fuzzing. For time-based brute-force
fuzzing of all components, use one of the following: `deepstate_2s`,
`deepstate_1m`, `deepstate_20m`, or `deepstate_8h`. To use individual fuzzers,
insert `art` or `qsbr`, for example: `deepstate_qsbr_20m` or `deepstate_art_8h`.
Running fuzzer under Valgrind is available through `valgrind_deepstate` for
everything or `valgrind_{art|qsbr}_deepstate` for individual fuzzers.

Fuzzers that use libfuzzer mirror the above by adding `_lf` before the time
suffix, such as `deepstate_lf_8h`, `deepstate_qsbr_lf_20m`,
`valgrind_deepstate_lf`, and so on.

## Related Projects

[art_map](https://github.com/justinasvd/art_map) is a C++14 template library
providing `std::`-like interface over the ART data structure. It shares some
code with UnoDB.

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
