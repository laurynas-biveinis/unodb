string(APPEND CMAKE_CXX_FLAGS " /fsanitize=address")

string(REGEX REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

add_subdirectory(3rd_party/googletest)

enable_testing()

add_executable(test_qsbr_ptr test/test_qsbr_ptr.cpp)
target_link_libraries(test_qsbr_ptr PRIVATE gtest_main)
