# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS "main.cpp"  # Replace "main.cpp" with the actual name of your main source file
    INCLUDE_DIRS "."
)
add_executable(main ${app_sources})
