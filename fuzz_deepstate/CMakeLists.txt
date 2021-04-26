# Copyright 2021 Laurynas Biveinis

enable_testing()

function(COMMON_DEEPSTATE_TARGET_PROPERTIES TARGET WITH_LIBFUZZER)
  # FIXME(laurynas): replace with cmake_parse_arguments
  if(WITH_LIBFUZZER)
    common_target_properties(${TARGET} SKIP_CHECKS)
    target_link_libraries(${TARGET} PRIVATE deepstate_lf)
    target_compile_options(${TARGET} PRIVATE "-fsanitize=fuzzer-no-link")
    target_link_libraries(${TARGET} PRIVATE "-fsanitize=fuzzer")
  else()
    common_target_properties(${TARGET})
    target_link_libraries(${TARGET} PRIVATE deepstate)
    set_clang_tidy_options(${TARGET} "${DO_CLANG_TIDY_DEEPSTATE}")
  endif()
endfunction()

set(DEEPSTATE_FAIL_DIR "deepstate_fails")
set(DEEPSTATE_OPTIONS "--fuzz" "--no_fork" "--output_test_dir"
  "${DEEPSTATE_FAIL_DIR}" "--timeout")

if(LIBFUZZER_AVAILABLE)
  set(DEEPSTATE_LF_OPTIONS "-use_value_profile=1" "-detect_leaks=0")
endif()

function(ADD_FUZZ_DEEPSTATE_TARGET ID)
  set(TEST_NAME "test_${ID}_fuzz_deepstate")
  set(TEST_5S "deepstate_${ID}_5s")
  set(TEST_1M "deepstate_${ID}_1m")
  set(TEST_20M "deepstate_${ID}_20m")
  set(TEST_8H "deepstate_${ID}_8h")
  set(VALGRIND_TARGET "valgrind_${ID}_deepstate")

  add_executable(${TEST_NAME} "${TEST_NAME}.cpp")
  common_deepstate_target_properties(${TEST_NAME} FALSE)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} ${DEEPSTATE_OPTIONS} 5)

  add_custom_target(${TEST_5S} DEPENDS ${TEST_NAME}
    COMMAND ${TEST_NAME} ${DEEPSTATE_OPTIONS} 5)
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

  if(LIBFUZZER_AVAILABLE)
    set(SOURCE_NAME "test_${ID}_fuzz_deepstate.cpp")
    set(TEST_NAME "test_${ID}_fuzz_deepstate_lf")
    set(CORPUS_DIR "deepstate_lf_corpus_${ID}")
    set(INVOCATION ${TEST_NAME} ${CORPUS_DIR} ${DEEPSTATE_LF_OPTIONS})
    set(TEST_5S "deepstate_lf_${ID}_5s")
    set(TEST_1M "deepstate_lf_${ID}_1m")
    set(TEST_20M "deepstate_lf_${ID}_20m")
    set(TEST_8H "deepstate_lf_${ID}_8h")
    set(VALGRIND_TARGET "valgrind_${ID}_deepstate_lf")

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CORPUS_DIR})

    add_executable(${TEST_NAME} "${SOURCE_NAME}")
    common_deepstate_target_properties(${TEST_NAME} TRUE)
    add_test(NAME ${TEST_NAME} COMMAND ${INVOCATION} "-max_total_time=5")

    add_custom_target(${TEST_5S} DEPENDS ${TEST_NAME}
      COMMAND ${INVOCATION} "-max-total-time=5")
    add_custom_target(${TEST_1M} COMMAND ${INVOCATION} "-max_total_time=60")
    add_custom_target(${TEST_20M} COMMAND ${INVOCATION} "-max_total_time=1200")
    add_custom_target(${TEST_8H} COMMAND ${INVOCATION} "-max_total_time=28800")

    add_dependencies(deepstate_lf_5s ${TEST_5S})
    add_dependencies(deepstate_lf_1m ${TEST_1M})
    add_dependencies(deepstate_lf_20m ${TEST_20M})
    add_dependencies(deepstate_lf_8h ${TEST_8H})

    add_custom_target(${VALGRIND_TARGET} COMMAND ${VALGRIND_COMMAND}
      ./${INVOCATION} 60)

    add_dependencies(valgrind_deepstate_lf ${VALGRIND_TARGET})
  endif()
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${DEEPSTATE_FAIL_DIR})

add_custom_target(deepstate_5s)
add_custom_target(deepstate_1m)
add_custom_target(deepstate_20m)
add_custom_target(deepstate_8h)
add_custom_target(valgrind_deepstate)

if(LIBFUZZER_AVAILABLE)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${DEEPSTATE_LF_CORPUS_DIR})
  add_custom_target(deepstate_lf_5s)
  add_custom_target(deepstate_lf_1m)
  add_custom_target(deepstate_lf_20m)
  add_custom_target(deepstate_lf_8h)
  add_custom_target(valgrind_deepstate_lf)
endif()

add_fuzz_deepstate_target(art)
target_link_libraries(test_art_fuzz_deepstate PRIVATE unodb)
if(LIBFUZZER_AVAILABLE)
  target_link_libraries(test_art_fuzz_deepstate_lf PRIVATE unodb_lf)
endif()

add_fuzz_deepstate_target(qsbr)
target_link_libraries(test_qsbr_fuzz_deepstate PRIVATE unodb_qsbr)
if(LIBFUZZER_AVAILABLE)
  target_link_libraries(test_qsbr_fuzz_deepstate_lf PRIVATE unodb_qsbr_lf)
endif()
