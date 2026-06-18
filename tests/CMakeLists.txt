add_executable(contour_tests contour_tests.cpp)
target_link_libraries(contour_tests PRIVATE ocrl_core)
target_compile_definitions(contour_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
ocrl_copy_runtime_dlls(contour_tests)
add_test(NAME contour_tests COMMAND contour_tests)
