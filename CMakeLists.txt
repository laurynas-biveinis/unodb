# Copyright 2019-2021 Laurynas Biveinis
cmake_minimum_required(VERSION 3.12)

project(unodb VERSION 0.1
  DESCRIPTION "unodb key-value store library"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

set(CLANG_CXX_WARNING_FLAGS
  "-Werror"
  # Warning groups
  "-Wall" "-Wextra" "-Wconversion" "-Wdelete-non-virtual-dtor" "-Wdeprecated"
  "-Wgnu" "-Wimplicit" "-Wloop-analysis" "-Wparentheses" "-Wpedantic"
  "-Wpragmas"
  "-Wself-assign" "-Wshadow-all"
  # Individual warnings
  "-Wabstract-vbase-init" "-Warray-bounds-pointer-arithmetic" "-Wassign-enum"
  "-Watomic-implicit-seq-cst" "-Wbad-function-cast" "-Wc++2a-compat"
  "-Wc++2a-extensions" "-Wcast-align" "-Wcast-qual" "-Wclass-varargs" "-Wcomma"
  "-Wconditional-uninitialized" "-Wcovered-switch-default" "-Wdate-time"
  "-Wdeprecated-implementations" "-Wdisabled-macro-expansion"
  "-Wdouble-promotion" "-Wduplicate-decl-specifier" "-Wduplicate-enum"
  "-Wduplicate-method-arg" "-Wduplicate-method-match" "-Wextra-semi-stmt"
  "-Wfloat-equal" "-Wformat-pedantic" "-Wformat=2" "-Wheader-hygiene"
  "-Widiomatic-parentheses" "-Wimplicit-fallthrough" "-Wmain"
  "-Wmethod-signatures" "-Wmissing-noreturn" "-Wmissing-prototypes"
  "-Wmissing-variable-declarations" "-Wnewline-eof" "-Wnon-virtual-dtor"
  "-Wnonportable-system-include-path" "-Wold-style-cast" "-Wover-aligned"
  "-Wpacked" "-Wpointer-arith" "-Wprofile-instr-missing" "-Wredundant-parens"
  "-Wreserved-id-macro" "-Wshift-sign-overflow" "-Wstatic-in-inline"
  "-Wstrict-prototypes" "-Wsuper-class-method-mismatch" "-Wswitch-enum"
  "-Wtautological-compare" "-Wtautological-constant-in-range-compare" "-Wundef"
  "-Wundefined-func-template" "-Wundefined-reinterpret-cast"
  "-Wunreachable-code-aggressive" "-Wunused-exception-parameter"
  "-Wunused-macros" "-Wunused-member-function" "-Wunused-template"
  "-Wused-but-marked-unused" "-Wvector-conversion" "-Wvla"
  "-Wweak-template-vtables" "-Wweak-vtables" "-Wzero-as-null-pointer-constant")
set(GCC_CXX_WARNING_FLAGS
  "-Werror"
  # Warning groups
  "-Wall" "-Wextra" "-Wpedantic" "-Wunused" "-Wparentheses" "-Wconversion"
  # Individual warnings
  "-Wabi-tag" "-Wcast-align=strict" "-Wcast-qual" "-Wcatch-value=3"
  "-Wctor-dtor-privacy" "-Wdouble-promotion" "-Wduplicated-branches"
  "-Wduplicated-cond" "-Wextra-semi" "-Wfloat-equal" "-Wformat-overflow=2"
  "-Wformat-signedness" "-Wformat-truncation=2" "-Wformat=2"
  "-Wimplicit-fallthrough=5" "-Winvalid-pch" "-Wlogical-op" "-Wmismatched-tags"
  "-Wmissing-declarations" "-Wmissing-include-dirs" "-Wnoexcept"
  "-Wnon-virtual-dtor" "-Wnull-dereference" "-Wold-style-cast"
  "-Woverloaded-virtual" "-Wpacked" "-Wplacement-new=2" "-Wredundant-decls"
  "-Wshadow=global" "-Wsign-conversion" "-Wsign-promo" "-Wstrict-null-sentinel"
  "-Wstringop-truncation" "-Wsuggest-attribute=cold" "-Wsuggest-attribute=const"
  "-Wsuggest-attribute=format" "-Wsuggest-attribute=malloc"
  "-Wsuggest-attribute=noreturn" "-Wsuggest-attribute=pure"
  "-Wsuggest-final-methods" "-Wsuggest-final-types" "-Wsuggest-override"
  "-Wswitch-enum" "-Wtrampolines" "-Wundef" "-Wuninitialized"
  "-Wunsafe-loop-optimizations" "-Wunused-const-variable=2" "-Wunused-macros"
  "-Wuseless-cast" "-Wvector-operation-performance" "-Wvla"
  "-Wzero-as-null-pointer-constant" "-Wattribute-alias=2" "-Warray-bounds=2"
  "-Wredundant-tags")
set(GCC_GE_11_CXX_WARNING_FLAGS
  "-Wctad-maybe-unsupported" "-Wdeprecated-enum-enum-conversion"
  "-Wdeprecated-enum-float-conversion" "-Wvexing-parse")

list(APPEND CXX_FLAGS "-g" "-msse4.1")

option(COVERAGE "Enable code coverage reporting")
if(COVERAGE)
  option(GCOV_PATH "gcov tool location to be used by lcov")
  if(GCOV_PATH)
    set(LCOV_GCOV_ARG "--gcov-tool" "${GCOV_PATH}")
    message(STATUS "Code coverage reporting enabled with gcov at ${GCOV_PATH}")
  else()
    set(LCOV_GCOV_ARG "")
    message(STATUS "Code coverage reporting enabled with default gcov path")
  endif()
else()
  message(STATUS "Code coverage reporting not enabled")
endif()

find_path(VALGRIND_H_PATH valgrind.h PATH_SUFFIXES valgrind)
if(VALGRIND_H_PATH)
  find_path(MEMCHECK_H_PATH memcheck.h PATH_SUFFIXES valgrind)
  if(MEMCHECK_H_PATH)
    message(STATUS
      "valgrind.h & memcheck.h found, enabling Valgrind client requests in debug config")
    set(VALGRIND_CLIENT ON)
  else()
    message(STATUS
      "valgrind.h found but memcheck.h not found, disabling Valgrind client requests in debug config")
  endif()
else()
  message(STATUS
    "valgrind.h not found, disabling Valgrind client requests in debug config")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # TODO(laurynas): switch to -Og once it actually works in the oldest
  # supported compilers
  list(APPEND CXX_FLAGS "-O0")
else()
  if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
      "${CMAKE_CXX_COMPILER_VERSION}" VERSION_GREATER_EQUAL 9)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_SUPPORT_ERROR LANGUAGES CXX)
    if(IPO_SUPPORTED)
      message(STATUS "IPO/LTO supported")
    else()
      message(STATUS "IPO/LTO is not supported: ${IPO_SUPPORT_ERROR}")
    endif()
  endif()
endif()

macro(ADD_TO_GNU_SANITIZER_FLAGS)
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list(APPEND SANITIZER_CXX_FLAGS ${ARGV})
    list(APPEND SANITIZER_LD_FLAGS ${ARGV})
  endif()
endmacro()

macro(SET_COMMON_SANITIZER_FLAGS)
  list(APPEND SANITIZER_CXX_FLAGS "-fno-omit-frame-pointer"
    "-fno-optimize-sibling-calls")
endmacro()

option(SANITIZE_ADDRESS "Enable AddressSanitizer runtime checks")
if(SANITIZE_ADDRESS)
  set_common_sanitizer_flags()
  list(APPEND SANITIZER_CXX_FLAGS "-fsanitize=address")
  list(APPEND SANITIZER_LD_FLAGS "-fsanitize=address")
  add_to_gnu_sanitizer_flags("-fsanitize=leak"
    "-fsanitize-address-use-after-scope" "-fsanitize=pointer-compare"
    "-fsanitize=pointer-subtract")
  string(CONCAT ASAN_ENV "ASAN_OPTIONS="
    "check_initialization_order=true:detect_stack_use_after_return=true:"
    "alloc_dealloc_mismatch=true:strict_string_checks=true")
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
      OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    # Not for GCC, up to version 10 at least, because:
    # 1) AddressSanitizer CHECK failed:
    # ../../../../src/libsanitizer/asan/asan_thread.cpp:369 "((bottom)) != (0)"
    # (0x0, 0x0)
    # https://bugs.llvm.org/show_bug.cgi?id=47626
    # 2) ERROR: AddressSanitizer: invalid-pointer-pair: 0x7fffe8404697
    # 0x000000000000
    # #0 0x55d21abf7a40 in std::__cxx11::basic_stringbuf<char,
    # std::char_traits<char>, std::allocator<char> >::str() const
    # /usr/include/c++/8/sstream:173
    string(APPEND ASAN_ENV ":detect_invalid_pointer_pairs=2")
  endif()
  if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    string(APPEND ASAN_ENV ":detect_leaks=1")
  endif()
  set(SANITIZER_ENV ${ASAN_ENV})
endif()

option(SANITIZE_THREAD "Enable ThreadSanitizer runtime checks")
if(SANITIZE_THREAD)
  set_common_sanitizer_flags()
  list(APPEND SANITIZER_CXX_FLAGS "-fsanitize=thread")
  list(APPEND SANITIZER_LD_FLAGS "-fsanitize=thread")
endif()

option(SANITIZE_UB "Enable UndefinedBehaviorSanitizer runtime checks")
if(SANITIZE_UB)
  set_common_sanitizer_flags()
  list(APPEND SANITIZER_CXX_FLAGS "-fsanitize=undefined")
  # Boost library 1.72 on macOS from Homebrew produces the following error:
  # SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior
  # /Users/laurynas/unodb/unodb/heap.hpp:83:8 in
  # /usr/local/include/boost/container/pmr/memory_resource.hpp:49:20: runtime
  # error: member call on address 0x000105abd0b
  # 0 which does not point to an object of type
  # 'boost::container::pmr::memory_resource'
  # 0x000105abd0b0: note: object is of type
  # 'boost::container::pmr::new_delete_resource_imp'
  # It looks like it's because new_delete_resource_imp is not exported as a
  # visible symbol in the library? I.e.
  # https://bugs.llvm.org/show_bug.cgi?id=39191,
  # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80963
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    list(APPEND SANITIZER_CXX_FLAGS "-fno-sanitize=vptr")
  endif()
  set(SANITIZER_LD_FLAGS "-fsanitize=undefined")
  string(CONCAT UBSAN_ENV "UBSAN_OPTIONS="
    "print_stacktrace=1:halt_on_error=1:abort_on_error=1")
  set(SANITIZER_ENV ${UBSAN_ENV})
endif()

option(STATIC_ANALYSIS "Enable compiler static analysis")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(UNODB_DETAIL_DEBUG "ON")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)

configure_file(config.hpp.in config.hpp)

find_package(Threads REQUIRED)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  message(STATUS "Using GCC, using std::pmr instead of boost::pmr")
else()
  list(APPEND BOOST_COMPONENTS container)
  # Workaround https://github.com/boostorg/boost_install/issues/13: Homebrew
  # Boost 1.71 installs single-thread and MT build to the same prefix, resulting
  # in -
  # CMake Warning at /usr/local/lib/cmake/boost_container-1.71.0/libboost_container-variant-shared.cmake:59 (message):
  # Target Boost::container already has an imported location
  # '/usr/local/lib/libboost_container-mt.dylib', which will be overwritten
  # with '/usr/local/lib/libboost_container.dylib'
  set(Boost_NO_BOOST_CMAKE ON)
endif()

find_package(Boost REQUIRED COMPONENTS ${BOOST_COMPONENTS})

string(REPLACE ";" " " CXX_FLAGS_FOR_SUBDIR_STR "${SANITIZER_CXX_FLAGS}")
string(APPEND CMAKE_CXX_FLAGS ${CXX_FLAGS_FOR_SUBDIR_STR})
string(REPLACE ";" " " LD_FLAGS_FOR_SUBDIR_STR "${SANITIZER_LD_FLAGS}")
string(APPEND CMAKE_EXE_LINKER_FLAGS "${LD_FLAGS_FOR_SUBDIR_STR}")
string(APPEND CMAKE_MODULE_LINKER_FLAGS "${LD_FLAGS_FOR_SUBDIR_STR}")
string(APPEND CMAKE_SHARED_LINKER_FLAGS "${LD_FLAGS_FOR_SUBDIR_STR}")
add_subdirectory(3rd_party/googletest)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing Google Benchmark tests"
  FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL
  "Suppressing Google Benchmark installation" FORCE)
if(IPO_SUPPORTED AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  set(BENCHMARK_ENABLE_LTO ON CACHE BOOL "Enabling LTO for Google Benchmark"
    FORCE)
  message(STATUS "Enabling LTO for Google Benchmark")
else()
  message(STATUS "Disabling LTO for Google Benchmark")
endif()
add_subdirectory(3rd_party/benchmark)

# Do not build DeepState with GCC under macOS due to
# https://github.com/trailofbits/deepstate/issues/374
if(NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND "${CMAKE_SYSTEM_NAME}"
      STREQUAL "Darwin"))
  # ThreadSanitizer is not compatible with libfuzzer and LLVM 11 linker crashes
  # on libfuzzer release build
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" AND NOT("${CMAKE_BUILD_TYPE}"
        STREQUAL "Release") AND NOT(SANITIZE_THREAD))
    set(LIBFUZZER_AVAILABLE TRUE)
    set(BUILD_DEEPSTATE_LIBFUZZER "-DDEEPSTATE_LIBFUZZER=ON")
  else()
    set(LIBFUZZER_AVAILABLE FALSE)
    set(BUILD_DEEPSTATE_LIBFUZZER "-DDEEPSTATE_LIBFUZZER=OFF")
  endif()

  include(ExternalProject)
  ExternalProject_Add(3rd_party_deepstate
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/3rd_party/deepstate"
    BINARY_DIR "${CMAKE_BINARY_DIR}/3rd_party/deepstate"
    CMAKE_ARGS "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCMAKE_C_FLAGS=-w"
    "-DCMAKE_CXX_FLAGS=-w" "${BUILD_DEEPSTATE_LIBFUZZER}"
    INSTALL_COMMAND "")
  ExternalProject_Get_property(3rd_party_deepstate SOURCE_DIR)
  ExternalProject_Get_property(3rd_party_deepstate BINARY_DIR)

  add_library(deepstate STATIC IMPORTED)
  add_dependencies(deepstate 3rd_party_deepstate)
  target_include_directories(deepstate INTERFACE "${SOURCE_DIR}/src/include/")
  set_target_properties(deepstate PROPERTIES IMPORTED_LOCATION
    "${BINARY_DIR}/libdeepstate.a")

  if(LIBFUZZER_AVAILABLE)
    add_library(deepstate_lf STATIC IMPORTED)
    add_dependencies(deepstate_lf 3rd_party_deepstate)
    target_include_directories(deepstate_lf INTERFACE
      "${SOURCE_DIR}/src/include/")
    set_target_properties(deepstate_lf PROPERTIES IMPORTED_LOCATION
      "${BINARY_DIR}/libdeepstate_LF.a")
  endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU"
    AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(benchmark PUBLIC _GLIBCXX_DEBUG
    _GLIBCXX_DEBUG_PEDANTIC)
endif()

# Add benchmark_include_dirs by target_include_directories(... SYSTEM ...)
# before target_link_libraries so that benchmark headers are included through
# -isystem not -I, resulting in build-breaking diagnostics.
get_target_property(benchmark_include_dirs benchmark::benchmark
  INTERFACE_INCLUDE_DIRECTORIES)

if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
    AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  message(STATUS "Not using clang-tidy due to non-clang compiler being used")
else()
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy"
    DOC "Path to clang-tidy executable")
  if(NOT CLANG_TIDY_EXE)
    message(STATUS "clang-tidy not found")
  else()
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-p=${CMAKE_BINARY_DIR}")
  endif()
endif()

set(CPPCHECK_ARGS
  # I actually do that
  "--suppress=accessMoved"
  # False positive on pointer unions being passed by value
  "--suppress=passedByValue"
  # False positive on Google Test TEST macro and compilers are much better
  # for syntax checking anyway
  "--suppress=syntaxError")

option(CPPCHECK_AGGRESSIVE "Enable inconclusive cppcheck checks")
if(CPPCHECK_AGGRESSIVE)
  list(APPEND CPPCHECK_ARGS "--inconclusive" "--error-exitcode=0")
else()
  list(APPEND CPPCHECK_ARGS "--error-exitcode=2")
endif()

find_program(CPPCHECK_EXE NAMES "cppcheck" DOC "Path to cppcheck executable")
if(NOT CPPCHECK_EXE)
  message(STATUS "cppcheck not found")
else()
  execute_process(COMMAND "${CPPCHECK_EXE}" "--version" OUTPUT_VARIABLE
    CPPCHECK_VERSION_OUTPUT)
  message(STATUS
    "cppcheck found: ${CPPCHECK_EXE}, --version: ${CPPCHECK_VERSION_OUTPUT}")
  set(DO_CPPCHECK "${CPPCHECK_EXE}"
    "--enable=warning,style,performance,portability" "-D__x86_64"
    "--inline-suppr")
  list(APPEND DO_CPPCHECK "${CPPCHECK_ARGS}")
endif()

find_program(CPPLINT_EXE NAMES "cpplint" DOC "Path to cpplint executable")
if(NOT CPPLINT_EXE)
  message(STATUS "cpplint not found")
else()
  message(STATUS "cpplint found: ${CPPLINT_EXE}")
endif()

option(IWYU "Enable include-what-you-use checking")
if(IWYU)
  if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
      AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    message(STATUS "Not using include-what-you-use due to non-clang compiler being used")
  else()
    find_program(IWYU_EXE NAMES "include-what-you-use"
      DOC "Path to include-what-you-use executable")
    if(NOT IWYU_EXE)
      message(STATUS "include-what-you-use not found")
    else()
      execute_process(COMMAND "${IWYU_EXE}" "--version" OUTPUT_VARIABLE
        IWYU_VERSION_OUTPUT)
      message(STATUS
        "include-what-you-use found: ${IWYU_EXE}, --version: ${IWYU_VERSION_OUTPUT}")
      set(DO_IWYU "${IWYU_EXE}")
    endif()
  endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(GSL_INCLUDES "3rd_party/GSL/include")

function(COMMON_TARGET_PROPERTIES TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 CTP "SKIP_CHECKS" "" "")
  target_compile_definitions(${TARGET} PRIVATE
    "$<$<AND:$<CONFIG:Debug>,$<BOOL:${VALGRIND_CLIENT}>>:VALGRIND_CLIENT_REQUESTS>")
  target_compile_options(${TARGET} PRIVATE
    "$<$<CXX_COMPILER_ID:AppleClang,Clang>:${CLANG_CXX_WARNING_FLAGS}>"
    "$<$<CXX_COMPILER_ID:GNU>:${GCC_CXX_WARNING_FLAGS}>"
    "$<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,11.0>>:${GCC_GE_11_CXX_WARNING_FLAGS}>"
    "$<$<CONFIG:Release>:$<IF:$<BOOL:${COVERAGE}>,-O0,-O3>>"
    "$<$<BOOL:${COVERAGE}>:--coverage>")
  target_compile_options(${TARGET} PRIVATE "${CXX_FLAGS}")
  target_compile_options(${TARGET} PRIVATE "${SANITIZER_CXX_FLAGS}")
  target_compile_features(${TARGET} PUBLIC cxx_std_17)
  set_target_properties(${TARGET} PROPERTIES CXX_EXTENSIONS OFF)
  # Change to target_link_options on 3.13 minimum CMake version
  target_link_libraries(${TARGET} PRIVATE "${LD_FLAGS}")
  target_link_libraries(${TARGET} PRIVATE "${SANITIZER_LD_FLAGS}")
  target_link_libraries(${TARGET} INTERFACE "${INTERFACE_LD_FLAGS}")
  target_link_libraries(${TARGET} INTERFACE "$<$<BOOL:${COVERAGE}>:--coverage>")
  if(IPO_SUPPORTED)
    set_target_properties(${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
  if(NOT CTP_SKIP_CHECKS)
    if(CPPCHECK_EXE)
      set_target_properties(${TARGET} PROPERTIES CXX_CPPCHECK "${DO_CPPCHECK}")
    endif()
    if(CPPLINT_EXE)
      set_target_properties(${TARGET} PROPERTIES CXX_CPPLINT "${CPPLINT_EXE}")
    endif()
    if(IWYU_EXE)
      set_target_properties(${TARGET} PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${DO_IWYU}")
    endif()
    if(STATIC_ANALYSIS)
      target_compile_options(${TARGET} PRIVATE "-fanalyzer")
      if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND
          "${CMAKE_CXX_COMPILER_VERSION}" VERSION_GREATER_EQUAL 11)
        # GCC 11 -fanalyzer not fully ready for C++ yet
        target_compile_options(${TARGET} PRIVATE
          "-Wno-analyzer-possible-null-dereference"
          "-Wno-analyzer-possible-null-argument")
      endif()
    endif()
  endif()
endfunction()

function(SET_CLANG_TIDY_OPTIONS TARGET COMMAND)
  if(DO_CLANG_TIDY)
    set_target_properties(${TARGET} PROPERTIES CXX_CLANG_TIDY "${COMMAND}")
  endif()
endfunction()

add_library(unodb_util INTERFACE)
# Move to add_library itself once CMake minimum version is 3.13
target_sources(unodb_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/global.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/debug_thread_sync.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/heap.hpp)
target_include_directories(unodb_util INTERFACE ".")
target_include_directories(unodb_util INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

function(ADD_UNODB_LIBRARY LIB)
  add_library(${LIB} ${ARGN})
  common_target_properties(${LIB})
  target_link_libraries(${LIB} PUBLIC unodb_util)
  target_include_directories(${LIB} SYSTEM PUBLIC "${GSL_INCLUDES}")
  set_clang_tidy_options(${LIB} "${DO_CLANG_TIDY}")

  if(LIBFUZZER_AVAILABLE)
    set(LIB_LF "${LIB}_lf")
    add_library(${LIB_LF} ${ARGN})
    common_target_properties(${LIB_LF} SKIP_CHECKS)
    target_link_libraries(${LIB_LF} PUBLIC unodb_util)
    target_include_directories(${LIB_LF} SYSTEM PUBLIC "${GSL_INCLUDES}")
    target_compile_options(${LIB_LF} PRIVATE "-fsanitize=fuzzer-no-link")
  endif()
endfunction()

add_unodb_library(unodb_qsbr qsbr.cpp qsbr.hpp qsbr_ptr.cpp qsbr_ptr.hpp)
target_include_directories(unodb_qsbr SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
target_link_libraries(unodb_qsbr PRIVATE "${Boost_LIBRARIES}")
target_link_libraries(unodb_qsbr PUBLIC Threads::Threads)
if(LIBFUZZER_AVAILABLE)
  target_include_directories(unodb_qsbr_lf SYSTEM PUBLIC "${Boost_INCLUDE_DIRS}")
  target_link_libraries(unodb_qsbr_lf PRIVATE "${Boost_LIBRARIES}")
  target_link_libraries(unodb_qsbr_lf PUBLIC Threads::Threads)
endif()

add_unodb_library(unodb art.cpp art.hpp art_common.cpp art_common.hpp
  mutex_art.hpp optimistic_lock.hpp art_internal_impl.hpp olc_art.hpp
  olc_art.cpp art_internal.cpp art_internal.hpp node_type.hpp)
target_link_libraries(unodb PUBLIC unodb_qsbr)
if(LIBFUZZER_AVAILABLE)
  target_link_libraries(unodb_lf PUBLIC unodb_qsbr_lf)
endif()

set(VALGRIND_COMMAND "valgrind" "--error-exitcode=1" "--leak-check=full"
  "--trace-children=yes")

add_custom_target(valgrind DEPENDS valgrind_tests valgrind_benchmarks)

enable_testing()

add_subdirectory(benchmark)
if(TARGET deepstate)
  add_subdirectory(fuzz_deepstate)
  add_dependencies(valgrind valgrind_deepstate)
endif()
add_subdirectory(test)
