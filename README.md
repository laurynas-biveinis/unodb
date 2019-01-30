# unodb

## Introduction

Unodb is an key-value store library. The main-memory component is ART
Trie. The licence is AGPLv3.

## Dependencies
*   git
*   a C++17 compiler, currently tested with clang 7.0 and GCC 8.0
*   CMake, at least 3.12
*   Guidelines Support Library for gsl::span, imported as a git
    submodule.
*   Boost.Container library
*   clang-format, at least 8.0
*   Google Test for tests, imported as a git submodule.
*   (optional) clang-tidy
*   (optional) cppcheck
*   (optional) cpplint
*   (optional) include-what-you-use

## Development

Source code is formatted with [Google C++ style][gc++style]. Automatic
code formatting is configured through git clean/fuzz filters. To
enable it, do `git config --local include.path ../.gitconfig`. If for
any reason you need to disable it temporarily, do `git config --local
--unset include.path`

clang-tidy, cppcheck, and cpplint will be invoked automatically during
build if found. Currently the diagnostic level for them as well as for
compiler warnings is set very high, and can be relaxed, especially for
clang-tidy, as need arises.

To enable Address, Leak, and Undefined Behavior sanitizers, add
-DSANITIZE=ON CMake option.

To invoke include-what-you-use, add -DIWYU=ON CMake option.

To enable inconclusive cppcheck diagnostics, add
-DCPPCHECK_AGGRESSIVE=ON CMake option.

Google Test is used for testing. There will be no unit tests for each
private implementation class.

## Literature

*ART Trie*: V. Leis, A. Kemper and T. Neumann, "The adaptive radix tree:
ARTful indexing for main-memory databases," 2013 29th IEEE
International Conference on Data Engineering (ICDE 2013)(ICDE),
Brisbane, QLD, 2013, pp. 38-49.
doi:10.1109/ICDE.2013.6544812

[gc++style]: https://google.github.io/styleguide/cppguide.html "Google C++ Style Guide"
