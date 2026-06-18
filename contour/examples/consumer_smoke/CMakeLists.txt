cmake_minimum_required(VERSION 3.21)
project(consumer_smoke CXX)
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
endif()
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE        Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     Debug)

find_package(occtcontour REQUIRED)

add_executable(consumer_smoke main.cpp)
target_link_libraries(consumer_smoke PRIVATE occtcontour::occtcontour)

if(WIN32)
    add_custom_command(TARGET consumer_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:consumer_smoke>"
                $<TARGET_RUNTIME_DLLS:consumer_smoke>
        COMMAND_EXPAND_LISTS)
endif()
