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

UnoDB source code is written in C++20, and relies on the following
platform-specific features:

* On Intel platforms, it requires SSE4.1 intrinsics (Nehalem and higher), AVX
  for MSVC builds, and optional AVX2 support, when available. This differs from
  the original ART paper, which only required SSE2.
* On ARM, it uses NEON intrinsics.

### Build dependencies

* Earliest versions of supported compilers: GCC 10, LLVM 11, XCode 16.1,
  MSVC 2022. Open an issue if you require support for an older version.
* CMake, at least 3.12
* Boost library. If building with statistics counters, then it is a mandatory
  dependency for Boost.Accumulator. It is also an optional dependency for
  Boost.Stacktrace.

### Build dependencies, bundled as git submodules

* Guidelines Support Library for `gsl::span`.
* Google Test for tests.
* Google Benchmark for microbenchmarks.
* [DeepState][deepstate] for fuzzing tests.

## Building

Out-of-source builds are recommended. Before anything else, do

``` bash
# --recursive is not strictly required at the moment, but a good habit to have
git submodule update --init --recursive
```

There are some CMake options for users:

* `-DSPINLOCK_LOOP=PAUSE|EMPTY` to choose the spinlock wait loop body
  implementation for the optimistic lock. `EMPTY` may benchmark better as long
  as there are fewer threads than available CPU cores. `PAUSE` will use that
  instruction on x86_64, and something similar on ARM. The default is `PAUSE`.
* `-DSTATS=OFF` if you want to compile away all the statistics counters. The
  result will scale better in benchmarks. The current stats implementation is
  global cache line-padded shared atomic counters. This option might be removed
  in the future if the stats are reimplemented with less overhead.
* `-DWITH_AVX2=OFF` to disable AVX2 intrinsics to use SSE4.1/AVX only.

There are other CMake options that are mainly intended for UnoDB development
itself and are discussed in [CONTRIBUTING.md](CONTRIBUTING.md).

## Platform-Specific Notes

### Ubuntu 22.04

``` bash
# libc6-dev-i386 is for DeepState
sudo apt-get install -y libboost-dev libc6-dev-i386
```

### Amazon Linux 2023

```bash
sudo dnf install git gcc g++ cmake boost-devel
# Optional, if you want to use Boost.Stacktrace:
git clone https://github.com/ianlancetaylor/libbacktrace
(cd libbacktrace && ./configure && make && sudo make install)
# Build as usual
```

### Amazon Linux 2

```bash
sudo yum install git gcc10 gcc10-c++ cmake3 boost-devel
# Pass -DCMAKE_C_COMPILER=gcc10-gcc -DCMAKE_CXX_COMPILER=gcc10-c++ to cmake3
#
# Benchmarking. For jemalloc, consider building a newer version from source.
sudo amazon-linux-extras install -y epel
sudo yum install jemalloc perf
```

## Usage

See `examples/` directory for simple usage examples.

All the declarations live in the `unodb` namespace, which is omitted in the
descriptions below.

The only currently supported key type is `std::uint64_t`, aliased as `key`. To
add a new simple key type, instantiate `art_key` type with the desired type, and
specialize `art_key::make_binary_comparable` according to the ART paper. Compound
keys or Unicode data should be handled by specifying gsl::span<std::byte> as the
key type.  In this case, the application must provide a span suitably encoded for
lexicographic comparisons (that is, one which is already binary compatible).

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
* `template <FN> scan(), scan_from(...), scan_range()` a family of
  scan methods, including forward and reverse scans and scans with a
  from_key and an exclusive upper bound to_key where fn is a lambda
  accepting a visitor and returning a bool indicating whether the scan
  should halt (bool halt).  See `examples/examples_art.cpp`.
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

### Adaptive Radix Tree

The implementation follows the paper description closely, with the following
differences and design choices:

* The paper algorithms are specified in SSE2 intrinsics. This implementation has
  SSE4.1 as the minimal level, and AVX2 as the default one on Intel. On ARM,
  NEON is used.
* Different ways to implement leaf nodes are discussed in the paper
  (single-value leaves, multi-value leaves, and combined pointer/value slots).
  Here single-value leaves are implemented.
* The paper discusses different choices in implementing search key path
  compression. Here the pessimistic path compression is implemented, with up to
  7 bytes of key data per internal node.

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

## Related Projects

[art_map](https://github.com/justinasvd/art_map) is a C++14 template library
providing `std::`-like interface over the ART data structure. It shares some
code with UnoDB.

## Contributing

Please see [CONTRIBUTING.md](CONTRIBUTING.md).

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

[deepstate]: https://github.com/trailofbits/deepstate "DeepState on GitHub"

[seqlock]: https://en.wikipedia.org/wiki/Seqlock
