<!--- -*- gfm -*- -->

# Contributing to UnoDB

## Optional development dependencies

- clang-format
- lcov
- clang-tidy
- clangd
- cppcheck
- cpplint
- include-what-you-use
- libfuzzer

## General workflow

Clone the repository, create your feature or fix branches, work on them, commit,
open a pull request to this repository.

## Development CMake options

The regular CMake option `-DCMAKE_BUILD_TYPE` is recognized. Setting it to
`Debug` will enable a lot of assertions, which are useful during development.

There also other development-specific options. All of them are `OFF` by default.

- `-DSTANDALONE=ON` always should be given when working on UnoDB itself, whereas
  for users with UnoDB as a part of another project it should be `OFF`. When
  turned on, it will build benchmarks by default and enable extra global debug
  checks that require entire programs to be compiled with them. Currently, this
  consists of the libstdc++ debug mode.
- `-DMAINTAINER_MODE=ON` to enable maintainer diagnostics. This makes
  compilation warnings fatal.
- `-DSANITIZE_ADDRESS=ON` to enable AddressSanitizer (asan) and, if available,
  LeakSanitizer. It is incompatible with the `-DSANITIZE_THREAD=ON` option.
- `-DSANITIZE_THREAD=ON` to enable ThreadSanitizer (tsan). It is incompatible
  with the `-DSANITIZE_ADDRESS=ON` option, not available under MSVC, and will
  disable libfuzzer support if it would be enabled otherwise.
- `-DSANITIZE_UB=ON` to enable UndefinedBehaviorSanitizer (ubsan). It is
  compatible with other sanitizer options, although some [false
  positive][sanitizer-combination-bug] might occur. Not available under MSVC.
- `-DSTATIC_ANALYSIS=ON` for GCC or MSVC compiler static analysis. LLVM analyzer
  is used without any CMake option, see the "Linting and static analysis"
  section below.
- `-DIWYU=ON` to use include-what-you-use. It will take effect if building with
  clang.
- `-DCPPCHECK_AGGRESSIVE=ON` to enable inconclusive cppcheck diagnostics. They
  will not fail a build.
- `-DCOVERAGE=ON` to generate coverage reports on tests, excluding fuzzers,
  using lcov.

## Code organization

Header files can be public and internal. The internal ones have "internal"
somewhere in their name.

Public API must be introduced in `unodb` namespace, and any such declarations
should appear in a public header. It is OK for the implementation of a public
declaration to be in a private header, but see below. All the private
declarations for implementation details should live in `unodb::detail`
namespace. Thus, private headers may contain both `unodb` and `unodb::detail`
code, but rethink the header split if the former becomes a majority in a header.

Macros should not be a part of public API, and may be used internally only when
unavoidable. If a macro has to be introduced, its name must be prefixed with
`UNODB_DETAIL_`.

## Code style guide

- The code should follow existing conventions, formatted with
  [Google C++ style][gc++style]. This is enforced by GitHub Actions SuperLinter
  running clang-format, currently version 21.
- Each source file must have a `// Copyright <file-intro-year>-<last-edit-year>
UnoDB contributors` as the first line.
- Each source file must `#include "global.hpp"` first thing.
- Identifiers should be `snake_case`.
- Type names should either have no suffix, or `_type` when declaring types from
  template parameters and other identifiers. It cannot be `_t` as this suffix is
  reserved by POSIX.
- The code is `noexcept`-maximalist. Every function and method that cannot throw
  should be marked as `noexcept`. If the code does not throw in release build
  but may throw in debug one, it should ignore the latter and be `noexcept`.
- The code is exception-safe, with strong exception guarantee. This is actually
  tested by [test/test_art_oom.cpp](test/test_art_oom.cpp) injecting
  `std::bad_alloc` into heap allocations. New code should exception-safe too,
  with OOM tests expanded as necessary.
- The code is `[[nodiscard]]`-maximalist. Every value-returning function and
  method starts out as `[[nodiscard]]` by default, and is only changed if there
  is a clear need to both handle and ignore the return value.
- The code follows the Almost Always Auto guideline.
- `const` should be used everywhere it is possible to do so, with the exception
  of by-value function parameters and class fields that support moving from.
- `constexpr` (and `consteval`) should be applied everywhere it is legal to do
  so. Perhaps one day we will have a compile-time Adaptive Radix Tree.
- All C++ standard library symbols must be namespace-qualified, and this
  includes symbols shared with C. For example `std::size_t`.
- Automatic code formatting can be configured through Git clean/fuzz filters. To
  enable this feature, do `git config --local include.path ../.gitconfig`. If
  you need to temporarily disable it, run `git config --local --unset include.path`.
- The code that cannot be possibly tested by short deterministic tests, for
  example, because it handles rarely-occurring non-deterministic concurrency
  conditions, should be excluded from coverage testing with `// LCOV_EXCL_LINE`
  comment for a single line or with `// LCOV_EXCL_START`, `// LCOV_EXCL_STOP`
  start and end markers for a block.

## Documentation style guide

- The code should be commented, but without comments repeating already obvious
  code.
- `TODO` comments may be used for future tasks. `FIXME` comments should be used
  for things that must be fixed before the code lands in the master branch,
  however sometimes they land there. In both cases they should have a username
  in parentheses, i.e. `TODO(alice)`, `FIXME(bob)`. It indicates the comment
  author, not necessarily who should address it.
- Doxygen is used to produce source code documentation. To build the local HTML
  docs, run `doxygen Doxyfile` from the root source directory.
- Doxygen commands should use `\foo` (and not `@foo`) syntax.
- The preferred location of the comments is next to the declarations. An
  exception is declarations with multiple conditionally compiled declarations,
  in which case a Doxygen comment with a `\def`, `\var`, or another suitable tag
  for the declaration should appear at the top of its section or source file,
  and it should also have a `\hideinitializer` tag.
- Doxygen automatic brief description detection is enabled, thus explicit
  `\brief` tags are not required, rather the first sentence in the Doxygen
  comment block will be interpreted as the brief description. It should use a
  headline-like style without articles.
- Markdown markup is preferred, i.e. `` `foo` `` instead of `\c foo`.

## Linting and static analysis

clang-tidy, cppcheck, and cpplint will be invoked automatically during the build
if found. The current diagnostic level for them, as well as for compiler
warnings, is set very high and can be relaxed if needed.

For GCC or MSVC static analysis, add `-DSTATIC_ANALYSIS=ON` CMake option as
listed in the "CMake options" section above. For LLVM, no special CMake option
is needed; instead prepend `scan-build` to `make`.

## Testing

Google Test and DeepState fuzzer are used for testing. For DeepState, both LLVM
libfuzzer and built-in fuzzer are supported.

To run the tests, do `ctest`. It is also useful to run tests in parallel with
e.g. `ctest -j10`. For verbose test output add `-V`.

Benchmarks can also serve as tests, especially under debug build and under
sanitizers or Valgrind. For that, there is a CMake target `quick_benchmarks`,
i.e. `make -j10 -k quick_benchmarks`. CI runs this target too.

To run everything (tests and quick benchmarks) under Valgrind, use `valgrind`
CMake target.

## Fuzzing

Fuzzer tests for ART and QSBR components are located in the `fuzz_deepstate`
subdirectory. The tests use DeepState with either a brute force or
libfuzzer-based backend. However, the only supported platforms are Linux (GCC &
clang) and macOS (clang only, no AddressSanitizer and ThreadSanitizer) under
x86_64 only.

Several Make targets are available for fuzzing. For time-based brute-force
fuzzing of all components, use one of the following: `deepstate_2s`,
`deepstate_1m`, `deepstate_20m`, or `deepstate_8h`. To use individual fuzzers,
insert `art` or `qsbr`, for example: `deepstate_qsbr_20m` or `deepstate_art_8h`.
Running fuzzer under Valgrind is available through `valgrind_deepstate` for
everything or `valgrind_{art|qsbr}_deepstate` for individual fuzzers.

Fuzzers that use libfuzzer mirror the above by adding `_lf` before the time
suffix, such as `deepstate_lf_8h`, `deepstate_qsbr_lf_20m`,
`valgrind_deepstate_lf`, and so on.

## Commit messages

- Keep the first line under 72 characters and don't finish it with a full stop.
- The second line should be empty.
- Use imperative mood ("Fix bug" not "fixes bug", nor "fixed bug").
- Reference fixed issues, i.e. "fixes: #123"

## Pull Requests

- Create one PR per feature or fix. If it is possible to split PR into
  independent smaller parts, do so.
- Include documentation updates for the changes, use Doxygen as needed.
- Code changes should be covered by small deterministic tests. The coverage
  target is 100% (to the achievable extent), after non-deterministic code has
  been annotated to be excluded from coverage as described in the "Style Guide"
  section above.
- A clean CI run is a prerequisite for merging the PR.
- In the case of merge conflicts, rebase.
- All commits should be squashed into logical units. If the PR has only one
  feature or fix, as it should, there should be only one commit in it.
- Very obvious changes may be pushed directly.

## Benchmarking

Benchmarking is hard: it is easy to do it incorrectly with no obvious warning
signs. Here are some suggestions how to set it up. They will not guarantee valid
results but should exclude some classes of invalid ones:

- Set up the benchmark machine for the best CPU counter observability and run
  repeatability. This configuration is at odds with some of the Linux security
  features, so don't do that on machines close to production:

  ```bash
  sudo sh -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"
  sudo sysctl -w kernel.kptr_restrict=0
  sudo sh -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"
  sudo sysctl -w vm.swappiness=0
  ```

- To make the above settings survive reboots, edit `/etc/sysctl.conf` for:

  ```conf
  kernel.perf_event_paranoid = -1
  kernel.kptr_restrict = 0
  vm.swappiness = 0
  ```

  and `/etc/sysctl.d/10-ptrace.conf` for `kernel.yama.ptrace_scope = 0`.

- On x86_64, make sure to use the performance governor for the CPU frequency
  management: edit `/etc/default/cpufrequtils` for `GOVERNOR="performance"`
  followed by `sudo /etc/init.d/cpufrequtils restart`. You can also do
  `sudo cpupower frequency-set --governor performance` for transient setting.
- Disable hyperthreading:
  `sudo sh -c "echo off > /sys/devices/system/cpu/smt/control"`
- Pick a CPU and shield it from other threads running there:
  `sudo cset shield --cpu=2 --kthread=on`
- Start a shell for benchmarking in the shield:
  `sudo cset shield --exec zsh`
- Use jemalloc for heap:
  `export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2`
- Now run your desired benchmarks. For the supported flags for i.e. benchmark
  filtering see [Google Benchmark
  documentation](https://github.com/google/benchmark/blob/main/docs/user_guide.md).
- To measure the effect of a patch, Google Benchmark provides
  [U-Test](https://github.com/google/benchmark/blob/main/docs/tools.md#u-test)
  feature. Let's say you changed N16 node fields, but only for the OLC case.
  Then you can run N16-specific tests, filtered for the OLC. The next command is
  in the directory with benchmark executables, and the baseline executables are
  at `/foo/bar/baseline`:

  ```bash
  python3 ../../../3rd_party/benchmark/tools/compare.py benchmarks \
     /foo/bar/baseline/micro_benchmark_olc \
     ./micro_benchmark_olc --benchmark_repetitions=9 --benchmark_filter='olc_db'
  ```

## License

By contributing, you agree that your contributions will be licensed under the
[LICENSE](LICENSE) terms.

[gc++style]: https://google.github.io/styleguide/cppguide.html "Google C++ Style Guide"
[sanitizer-combination-bug]: https://github.com/google/sanitizers/issues/1106
