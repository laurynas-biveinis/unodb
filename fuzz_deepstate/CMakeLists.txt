# Copyright 2021 Laurynas Biveinis

enable_testing()

include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
check_cxx_source_compiles(
  "#include <cstddef>
     #include <cstdint>
     extern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t *, std::size_t) {
       return 0;
     }" LIBFUZZER_OK)

if(LIBFUZZER_OK)
  find_library(DEEPSTATE_LF_LIB_PATH deepstate_LF)
  if(NOT DEEPSTATE_LF_LIB_PATH)
    message(STATUS "libfuzzer-enabled DeepState not found, not building its fuzz tests")
  else()
    message(STATUS "libfuzzer-enabled DeepState found in ${DEEPSTATE_LF_LIB_PATH}")
  endif()
endif()

if(DEEPSTATE_LF_LIB_PATH)
  set(DEEPSTATE_LF_OK TRUE)
endif()

function(COMMON_DEEPSTATE_TARGET_PROPERTIES TARGET)
  common_target_properties(${TARGET})
  target_include_directories(${TARGET} SYSTEM PRIVATE
    "${DEEPSTATE_INCLUDE_PATH}")
  target_link_libraries(${TARGET} PRIVATE unodb deepstate)
  set_clang_tidy_options(${TARGET} "${DO_CLANG_TIDY_DEEPSTATE}")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug"
      AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # Workaround https://github.com/trailofbits/deepstate/issues/375 -
    # In file included from
    # /home/travis/build/laurynas-biveinis/unodb/3rd_party/deepstate/src/include/deepstate/DeepState.hpp:20,
    # from
    # /home/travis/build/laurynas-biveinis/unodb/fuzz_deepstate/test_art_fuzz_deepstate.cpp:8:
    # /home/travis/build/laurynas-biveinis/unodb/3rd_party/deepstate/src/include/deepstate/DeepState.h: In function ‘size_t deepstate::PickIndex(double*, size_t)’:
    # /home/travis/build/laurynas-biveinis/unodb/3rd_party/deepstate/src/include/deepstate/DeepState.h:392:1: error: inlining failed in call to ‘always_inline’ ‘uint32_t DeepState_UIntInRange(uint32_t, uint32_t)’: function not considered for inlining
    # 392 | DEEPSTATE_MAKE_SYMBOLIC_RANGE(UInt, uint32_t)
    #      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    # /home/travis/build/laurynas-biveinis/unodb/3rd_party/deepstate/src/include/deepstate/DeepState.h:392:1: note: called from here
    # 392 | DEEPSTATE_MAKE_SYMBOLIC_RANGE(UInt, uint32_t)
    #      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    target_compile_options(${TARGET} PRIVATE "-O1")
  endif()
endfunction()

set(DEEPSTATE_FAIL_DIR "deepstate_fails")
set(DEEPSTATE_OPTIONS "--fuzz" "--output_test_dir" "${DEEPSTATE_FAIL_DIR}"
  "--timeout")

function(ADD_FUZZ_DEEPSTATE_TARGET ID)
  set(TEST_NAME "test_${ID}_fuzz_deepstate")
  set(TEST_5S "deepstate_${ID}_5s")
  set(TEST_1M "deepstate_${ID}_1m")
  set(TEST_20M "deepstate_${ID}_20m")
  set(TEST_8H "deepstate_${ID}_8h")
  set(VALGRIND_TARGET "valgrind_${ID}_deepstate")

  add_executable(${TEST_NAME} "${TEST_NAME}.cpp")
  common_deepstate_target_properties(${TEST_NAME})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} ${DEEPSTATE_OPTIONS} 5)

  add_custom_target(${TEST_5S} DEPENDS ${TEST_NAME})
  add_custom_target(${TEST_1M} COMMAND ${TEST_NAME} ${DEEPSTATE_OPTIONS} 60)
  add_custom_target(${TEST_20M} COMMAND ${TEST_NAME} ${DEEPSTATE_OPTIONS} 1200)
  add_custom_target(${TEST_8H} COMMAND ${TEST_NAME} ${DEEPSTATE_OPTIONS} 28800)

  add_dependencies(deepstate_5s ${TEST_5S})
  add_dependencies(deepstate_1m ${TEST_1M})
  add_dependencies(deepstate_20m ${TEST_20M})
  add_dependencies(deepstate_8h ${TEST_8H})

  add_custom_target(${VALGRIND_TARGET} COMMAND ${VALGRIND_COMMAND}
    ./${TEST_NAME} ${DEEPSTATE_OPTIONS} 60)

  add_dependencies(valgrind_deepstate ${VALGRIND_TARGET})
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${DEEPSTATE_FAIL_DIR})

add_custom_target(deepstate_5s)
add_custom_target(deepstate_1m)
add_custom_target(deepstate_20m)
add_custom_target(deepstate_8h)
add_custom_target(valgrind_deepstate)

add_fuzz_deepstate_target(art)
add_fuzz_deepstate_target(qsbr)

if(DEEPSTATE_LF_OK)
  add_executable(test_art_fuzz_deepstate_lf test_art_fuzz_deepstate.cpp)
  add_test(NAME test_art_fuzz_deepstate_lf COMMAND test_art_fuzz_deepstate_lf -runs=1)
  common_deepstate_target_properties(test_art_fuzz_deepstate_lf)
  set_target_properties(test_art_fuzz_deepstate_lf PROPERTIES COMPILE_DEFINITIONS "LIBFUZZER")
  target_compile_options(test_art_fuzz_deepstate_lf PRIVATE "-fsanitize=fuzzer")
  target_link_libraries(test_art_fuzz_deepstate_lf PRIVATE "${DEEPSTATE_LF_LIB_PATH}")
  target_link_libraries(test_art_fuzz_deepstate_lf PRIVATE "-fsanitize=fuzzer")

  add_custom_target(deepstate_lf_1m
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${SANITIZER_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=60)

  add_custom_target(deepstate_lf_20m
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${SANITIZER_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=1200)

  add_custom_target(deepstate_lf_8h
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${SANITIZER_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=28800)
endif()
