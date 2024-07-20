cmake_minimum_required(VERSION 3.12)

project(Render)
find_package(OpenGL REQUIRED)

include_directories(/usr/include/drm)
link_directories(/usr/local/lib/x86_64-linux-gnu)
add_executable(Render render_cube.cpp)

target_link_libraries(${PROJECT_NAME} EGL GLESv2 drm gbm)
