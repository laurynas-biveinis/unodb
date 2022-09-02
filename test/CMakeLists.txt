enable_testing()

add_executable(test_qsbr_ptr test_qsbr_ptr.cpp)
common_target_properties(test_qsbr_ptr)
target_link_libraries(test_qsbr_ptr PRIVATE gtest_main)
