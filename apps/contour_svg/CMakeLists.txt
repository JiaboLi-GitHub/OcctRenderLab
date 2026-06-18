add_executable(contour_svg main.cpp)
target_link_libraries(contour_svg PRIVATE ocrl_core)
ocrl_copy_runtime_dlls(contour_svg)
