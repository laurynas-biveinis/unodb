# Copyright 2024 Laurynas Biveinis

add_executable(example_art example_art.cpp)
common_target_properties(example_art)
target_link_libraries(example_art PRIVATE unodb)

add_executable(example_art_stats example_art_stats.cpp)
common_target_properties(example_art_stats)
target_link_libraries(example_art_stats PRIVATE unodb)

add_custom_target(examples
  env ${SANITIZER_ENV} ./example_art
  COMMAND env ${SANITIZER_ENV} ./example_art_stats
  DEPENDS example_art example_art_stats)

add_custom_target(valgrind_examples
  COMMAND ${VALGRIND_COMMAND} ./example_art
  COMMAND ${VALGRIND_COMMAND} ./example_art_stats
  DEPENDS example_art example_art_stats)
