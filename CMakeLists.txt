cmake_minimum_required(VERSION 3.12)

project(unodb VERSION 0.1
  DESCRIPTION "unodb key-value store library"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
    OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  set(CXX_WARNING_FLAGS
    # Warning groups
    "-Wall" "-Wextra" "-Wconversion" "-Wdeprecated" "-Wgnu" "-Wimplicit"
    "-Wloop-analysis" "-Wpedantic" "-Wpragmas"  "-Wself-assign" "-Wshadow-all"
    "-Wthread-safety"
    # Individual warnings
    "-Wabstract-vbase-init" "-Warray-bounds-pointer-arithmetic" "-Wassign-enum"
    "-Wbad-function-cast" "-Wc++2a-compat" "-Wc++2a-extensions" "-Wcast-align"
    "-Wcast-qual" "-Wclass-varargs" "-Wcomma" "-Wconditional-uninitialized"
    "-Wcovered-switch-default" "-Wdate-time" "-Wdeprecated-implementations"
    "-Wdisabled-macro-expansion" "-Wdouble-promotion" "-Wduplicate-decl-specifier"
    "-Wduplicate-enum" "-Wduplicate-method-arg" "-Wduplicate-method-match"
    "-Wfloat-equal" "-Wformat-pedantic" "-Wformat=2" "-Wheader-hygiene"
    "-Widiomatic-parentheses" "-Wimplicit-fallthrough" "-Wmain"
    "-Wmethod-signatures" "-Wmissing-noreturn" "-Wmissing-prototypes"
    "-Wmissing-variable-declarations" "-Wnewline-eof" "-Wnon-virtual-dtor"
    "-Wnonportable-system-include-path" "-Wold-style-cast" "-Wover-aligned"
    "-Wpacked" "-Wpointer-arith" "-Wprofile-instr-missing" "-Wredundant-parens"
    "-Wreserved-id-macro" "-Wshift-sign-overflow" "-Wstatic-in-inline"
    "-Wstrict-prototypes" "-Wsuper-class-method-mismatch" "-Wswitch-enum"
    "-Wtautological-compare" "-Wtautological-constant-in-range-compare"
    "-Wthread-safety-beta" "-Wthread-safety-negative" "-Wthread-safety-verbose"
    "-Wundef" "-Wundefined-func-template" "-Wundefined-reinterpret-cast"
    "-Wunreachable-code-aggressive" "-Wunused-exception-parameter"
    "-Wunused-macros" "-Wunused-member-function" "-Wunused-template"
    "-Wused-but-marked-unused" "-Wvector-conversion" "-Wvla"
    "-Wweak-template-vtables" "-Wweak-vtables" "-Wzero-as-null-pointer-constant")
else()
  set(CXX_WARNING_FLAGS
    # Warning groups
    "-Wall" "-Wextra" "-Wpedantic" "-Wunused" "-Wparentheses" "-Wconversion"
    # Individual warnings
    "-Wdouble-promotion" "-Wformat-overflow=2" "-Wformat=2" "-Wformat-signedness"
    "-Wformat-truncation=2" "-Wnull-dereference" "-Wimplicit-fallthrough=5"
    "-Wmissing-include-dirs" "-Wswitch-enum" "-Wunused-const-variable=2"
    "-Wuninitialized" "-Wstringop-truncation" "-Wsuggest-attribute=pure"
    "-Wsuggest-attribute=const" "-Wsuggest-attribute=noreturn"
    "-Wsuggest-attribute=format" "-Wsuggest-attribute=cold"
    "-Wsuggest-attribute=malloc" "-Wsuggest-final-types"
    "-Wsuggest-final-methods" "-Wsuggest-override" "-Wduplicated-branches"
    "-Wduplicated-cond" "-Wtrampolines" "-Wfloat-equal" "-Wshadow=global"
    "-Wunsafe-loop-optimizations" "-Wplacement-new=2" "-Wundef" "-Wunused-macros"
    "-Wcast-qual" "-Wcast-align=strict" "-Wcatch-value=3"
    "-Wzero-as-null-pointer-constant" "-Wuseless-cast" "-Wextra-semi"
    "-Wsign-conversion" "-Wlogical-op" "-Wmissing-declarations" "-Wpacked"
    "-Wredundant-decls" "-Winvalid-pch" "-Wvector-operation-performance" "-Wvla"
    # C++-specific warnings
    "-Wabi-tag" "-Wctor-dtor-privacy" "-Wnoexcept" "-Wnon-virtual-dtor"
    "-Wstrict-null-sentinel" "-Wold-style-cast" "-Woverloaded-virtual"
    "-Wsign-promo")
endif()

list(APPEND CXX_FLAGS "${CXX_WARNING_FLAGS}" "-O1" "-g")

if(NOT CMAKE_BUILD_TYPE MATCHES "Debug")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_SUPPORT_ERROR LANGUAGES CXX)
  if(NOT IPO_SUPPORTED)
    message(STATUS "IPO/LTO is not supported: ${IPO_SUPPORT_ERROR}")
  endif()
endif()

option(SANITIZE "Enable all Sanitizer runtime checks")
if(SANITIZE)
  set(GCC_EXTRA_SANITIZE_FLAGS "-fsanitize=pointer-compare"
    "-fsanitize=pointer-subtract" "-fsanitize=leak"
    "-fsanitize-address-use-after-scope")
  list(APPEND CXX_FLAGS
    "-fno-omit-frame-pointer" "-fno-optimize-sibling-calls" "-fsanitize=address"
    "-fsanitize=undefined")
  set(LD_FLAGS "-fsanitize=address" "-fsanitize=undefined")
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list(APPEND CXX_FLAGS "${GCC_EXTRA_SANITIZE_FLAGS}")
    list(APPEND LD_FLAGS "${GCC_EXTRA_SANITIZE_FLAGS}")
  endif()
  string(CONCAT ASAN_OPTIONS "ASAN_OPTIONS="
    "detect_leaks=1:check_initialization_order=true:"
    "detect_stack_use_after_return=true:alloc_dealloc_mismatch=true:"
    "strict_string_checks=true:detect_invalid_pointer_pairs=2")
  string(CONCAT UBSAN_OPTIONS "UBSAN_OPTIONS="
    "print_stacktrace=1")
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
  SET(DEBUG "ON")
endif()

configure_file(config.hpp.in config.hpp)

find_package(Boost REQUIRED COMPONENTS container)

find_program(CLANG_TIDY_EXE NAMES "clang-tidy"
  DOC "Path to clang-tidy executable")
if(NOT CLANG_TIDY_EXE)
  message(STATUS "clang-tidy not found")
else()
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  string(CONCAT CLANG_TIDY_DISABLED
    "-fuchsia-default-arguments,"
    "-cppcoreguidelines-avoid-magic-numbers,"
    "-cppcoreguidelines-macro-usage," # Until it respects __LINE__ in macro definition
    "-cppcoreguidelines-non-private-member-variables-in-classes,"
    "-cppcoreguidelines-pro-bounds-array-to-pointer-decay,"
    "-cppcoreguidelines-pro-bounds-constant-array-index,"
    # Because leaf nodes are std::byte arrays
    "-cppcoreguidelines-pro-bounds-pointer-arithmetic,"
    "-cppcoreguidelines-pro-type-member-init,"
    "-cppcoreguidelines-pro-type-reinterpret-cast,"
    "-cppcoreguidelines-pro-type-static-cast-downcast,"
    "-cppcoreguidelines-pro-type-union-access,"
    "-fuchsia-overloaded-operator,"
    "-google-readability-braces-around-statements,"
    "-google-runtime-references,"
    "-hicpp-braces-around-statements,"
    "-hicpp-member-init,"
    "-hicpp-no-array-decay,"
    "-hicpp-use-equals-default,"
    "-llvm-include-order,"
    "-misc-non-private-member-variables-in-classes,"
    "-modernize-use-equals-default," # Until foo() noexcept = default is accepted by clang
    "-readability-braces-around-statements,"
    "-readability-else-after-return," # Until clang-tidy learns about constexpr if
    "-readability-magic-numbers")
  set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-p=${CMAKE_BINARY_DIR}" "-checks=*,${CLANG_TIDY_DISABLED}")
  string(CONCAT CLANG_TIDY_DISABLED_FOR_TESTS
    "${CLANG_TIDY_DISABLED},"
    "-build-include-order,"
    "-cert-err58-cpp,"
    "-cppcoreguidelines-avoid-goto,"
    "-cppcoreguidelines-owning-memory,"
    "-cppcoreguidelines-pro-type-vararg," # GTest uses varargs
    "-cppcoreguidelines-special-member-functions,"
    "-fuchsia-statically-constructed-objects,"
    "-google-runtime-references,"
    "-hicpp-avoid-goto,"
    "-hicpp-special-member-functions,"
    "-hicpp-vararg") # GTest uses varargs
  set(DO_CLANG_TIDY_TEST "${CLANG_TIDY_EXE}" "-p=${CMAKE_BINARY_DIR}"
    "-checks=*,${CLANG_TIDY_DISABLED_FOR_TESTS}")
endif()

option(CPPCHECK_AGGRESSIVE "Enable inconclusive cppcheck checks")
if(CPPCHECK_AGGRESSIVE)
  set(CPPCHECK_AGGRESSIVE_OPT "--inconclusive")
else()
  set(CPPCHECK_AGGRESSIVE_OPT "")
endif()

find_program(CPPCHECK_EXE NAMES "cppcheck" DOC "Path to cppcheck executable")
if(NOT CPPCHECK_EXE)
  message(STATUS "cppcheck not found")
else()
  message(STATUS "cppcheck found: ${CPPCHECK_EXE}")
  set(DO_CPPCHECK "${CPPCHECK_EXE}" "--enable=warning,style,performance,portability"
    "--suppress=noConstructor" "--suppress=syntaxError" "--suppress=unreadVariable"
    "${CPPCHECK_AGGRESSIVE_OPT}")
endif()

find_program(CPPLINT_EXE NAMES "cpplint" DOC "Path to cpplint executable")
if(NOT CPPLINT_EXE)
  message(STATUS "cpplint not found")
else()
  message(STATUS "cpplint found: ${CPPLINT_EXE}")
  string(CONCAT CPPLINT_DISABLED_TESTS "--filter="
    "-build/include_order,"
    "-runtime/references,"
    "-whitespace/braces") # Does not understand C++17 structured bindings and we use
                          # clang-format anyway
  set(DO_CPPLINT "${CPPLINT_EXE}" "${CPPLINT_DISABLED_TESTS}")
endif()

option(IWYU "Enable include-what-you-use checking")
if(IWYU)
  find_program(IWYU_EXE NAMES "include-what-you-use"
    DOC "Path to include-what-you-use executable")
  if(NOT IWYU_EXE)
    message(STATUS "include-what-you-use not found")
  else()
    message(STATUS "include-what-you-use found: ${IWYU_EXE}")
    set(DO_IWYU "${IWYU_EXE}")
  endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(GSL_INCLUDES "GSL/include")

add_library(unodb art.cpp art.hpp)
target_include_directories(unodb PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(unodb SYSTEM PUBLIC "${GSL_INCLUDES}")
target_include_directories(unodb SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
target_link_libraries(unodb PRIVATE "${Boost_LIBRARIES}")
# Change to target_link_options on 3.13 minimum CMake version
target_link_libraries(unodb PRIVATE "${LD_FLAGS}")
target_compile_features(unodb PUBLIC cxx_std_17)
target_compile_options(unodb PRIVATE "${CXX_FLAGS}")
set_target_properties(unodb PROPERTIES CXX_EXTENSIONS OFF)
if(IPO_SUPPORTED)
  set_target_properties(unodb PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
if(DO_CLANG_TIDY)
  set_target_properties(unodb PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY}")
endif()
if(CPPCHECK_EXE)
  set_target_properties(unodb PROPERTIES CXX_CPPCHECK "${DO_CPPCHECK}")
endif()
if(CPPLINT_EXE)
  set_target_properties(unodb PROPERTIES CXX_CPPLINT "${DO_CPPLINT}")
endif()
if(IWYU_EXE)
  set_target_properties(unodb PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${DO_IWYU}")
endif()

add_subdirectory(googletest)

enable_testing()

add_executable(test_art test_art.cpp)
target_include_directories(test_art PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(test_art PRIVATE "${CXX_FLAGS}")
target_link_libraries(test_art PRIVATE unodb gtest_main)
target_link_libraries(test_art PRIVATE "${LD_FLAGS}")
if(IPO_SUPPORTED)
  set_target_properties(test_art PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
if(DO_CLANG_TIDY)
  set_target_properties(test_art PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY_TEST}")
endif()
if(CPPCHECK_EXE)
  set_target_properties(test_art PROPERTIES CXX_CPPCHECK "${DO_CPPCHECK}")
endif()
if(CPPLINT_EXE)
  set_target_properties(test_art PROPERTIES CXX_CPPLINT "${DO_CPPLINT}")
endif()
if(IWYU_EXE)
  set_target_properties(test_art PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${DO_IWYU}")
endif()
add_test(test_art test_art)
if(SANITIZE)
  set_tests_properties(test_art PROPERTIES ENVIRONMENT "${ASAN_OPTIONS}")
  set_tests_properties(test_art PROPERTIES ENVIRONMENT "${UBSAN_OPTIONS}")
endif()
