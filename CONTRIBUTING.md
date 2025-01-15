<!--- -*- gfm -*- -->

# Contributing to UnoDB

## Development dependencies

* clang-format
* lcov
* clang-tidy
* clangd
* cppcheck
* cpplint
* include-what-you-use
* libfuzzer

## Development CMake options

* `-DSTANDALONE=ON` always should be given when working on UnoDB itself, whereas
  for users with UnoDB as a part of another project it should be `OFF`. When
  turned on, it will enable extra global debug checks that require entire
  programs to be compiled with them. Currently, this consists of the libstdc++
  debug mode. The default is `OFF`.
* `-DMAINTAINER_MODE=ON` to enable maintainer diagnostics. This makes
  compilation warnings fatal.
* `-DSANITIZE_ADDRESS=ON` to enable AddressSanitizer and, if available,
  LeakSanitizer. It is incompatible with the `-DSANITIZE_THREAD=ON` option.
* `-DSANITIZE_THREAD=ON` to enable ThreadSanitizer. It is incompatible with the
  `-DSANITIZE_ADDRESS=ON` option, not available under MSVC, and will disable
  libfuzzer support if it would be enabled otherwise.
* `-DSANITIZE_UB=ON` to enable UndefinedBehaviorSanitizer. It is compatible with
  other sanitizer options, although some
  [false positive][sanitizer-combination-bug] might occur. Not available under
  MSVC.
* `-DSTATIC_ANALYSIS=ON` for GCC or MSVC compiler static analysis. LLVM analyzer
  is used without any CMake option, see the "Linting and static analysis"
  section below.
* `-DIWYU=ON` to use include-what-you-use. It will take effect if building with
  clang.
* `-DCPPCHECK_AGGRESSIVE=ON` to enable inconclusive cppcheck diagnostics. They
  will not fail a build.
* `-DCOVERAGE=ON` to generate coverage reports on tests, excluding fuzzers,
  using lcov.

## Style Guide

* The code should follow existing conventions, formatted with
  [Google C++ style][gc++style].
* Automatic code formatting is configured through git clean/fuzz filters. To
  enable this feature, do `git config --local include.path ../.gitconfig`. If
  you need to temporarily disable it, run `git config --local --unset
  include.path`.

## Linting and static analysis

clang-tidy, cppcheck, and cpplint will be invoked automatically during the build
if found. The current diagnostic level for them, as well as for compiler
warnings, is set very high and can be relaxed if needed.

For GCC or MSVC static analysis, add `-DSTATIC_ANALYSIS=ON` CMake option as
listed in the "CMake options" section above. For LLVM, no special CMake option
is needed; instead prepend `scan-build` to `make`.

## Testing

Google Test and DeepState fuzzer are used for testing. There will be no unit
tests for each private implementation class. For DeepState, both LLVM libfuzzer
and built-in fuzzer are supported.

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

## Commit messages

* Keep the first line under 72 characters and don't finish it with a full stop.
* The second line should be empty.
* Use imperative mood ("Fix bug" not "fixes bug", nor "fixed bug").
* Reference fixed issues, i.e. "fixes: #123"

## Pull Requests

* Create one PR per feature or fix. If it is possible to split PR into
  independent smaller parts, do so.
* Include documentation updates for the changes.
* Code changes should be covered by small deterministic tests.

## License

By contributing, you agree that your contributions will be licensed under
LICENSE terms.

[gc++style]: https://google.github.io/styleguide/cppguide.html
"Google C++ Style Guide"

[sanitizer-combination-bug]: https://github.com/google/sanitizers/issues/1106
