cmake_minimum_required(VERSION 3.16)
project(mySoftRender VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(SDL2 REQUIRED)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.9
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glm stb imgui)

add_library(stb_image_wrapper STATIC
    src/stb_image_impl.cpp
)

target_include_directories(stb_image_wrapper PUBLIC
    ${stb_SOURCE_DIR}
)

add_library(imgui_sdl STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

target_include_directories(imgui_sdl PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_sdl PUBLIC SDL2::SDL2)

add_executable(mySoftRender
    src/main.cpp
    src/software_renderer.cpp
)

target_include_directories(mySoftRender PRIVATE include)

target_link_libraries(mySoftRender PRIVATE
    SDL2::SDL2
    glm::glm
    stb_image_wrapper
    imgui_sdl
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(mySoftRender PRIVATE SDL2::SDL2main)
endif()
