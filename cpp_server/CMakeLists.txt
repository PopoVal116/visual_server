cmake_minimum_required(VERSION 3.14)
project(cpp_server)

set(CMAKE_CXX_STANDARD 17)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR         ${PROJECT_SOURCE_DIR}/src)
set(INCLUDE_DIR     ${PROJECT_SOURCE_DIR}/include)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PostgreSQL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

include_directories(
    ${INCLUDE_DIR}
    ${ZMQ_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
)

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC 
    ${THIRD_PARTY_DIR}/imgui 
    ${THIRD_PARTY_DIR}/imgui/backends
)
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_compile_options(implot PRIVATE -fPIC)
target_link_libraries(implot PUBLIC imgui)

add_executable(server 
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/common.cpp
    ${SRC_DIR}/database.cpp
    ${SRC_DIR}/server.cpp
    ${SRC_DIR}/gui.cpp
)

target_link_libraries(server PRIVATE 
    implot 
    ${ZMQ_LIBRARIES}
    ${SDL2_LIBRARIES} 
    ${OPENGL_LIBRARIES} 
    ${GLEW_LIBRARIES}
    PostgreSQL::PostgreSQL
    pthread
    dl 
)

set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-g -O0 -Wall)
