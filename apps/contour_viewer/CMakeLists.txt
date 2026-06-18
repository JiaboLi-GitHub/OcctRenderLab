add_library(ocrl_render STATIC OutlineRender.cpp)
target_link_libraries(ocrl_render PUBLIC ocrl_core vsg::vsg)
target_include_directories(ocrl_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(outline_render_selfcheck selfcheck.cpp)
target_link_libraries(outline_render_selfcheck PRIVATE ocrl_render)
ocrl_copy_runtime_dlls(outline_render_selfcheck)

add_executable(contour_viewer main.cpp)
target_link_libraries(contour_viewer PRIVATE ocrl_render)
ocrl_copy_runtime_dlls(contour_viewer)
