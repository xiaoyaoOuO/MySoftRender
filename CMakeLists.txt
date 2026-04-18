cmake_minimum_required(VERSION 3.16)
project(mySoftRender VERSION 0.1 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)
if(MINGW AND TARGET SDL2::SDL2)
    get_target_property(SDL2_INTERFACE_INCLUDE_DIRS SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    if(SDL2_INTERFACE_INCLUDE_DIRS)
        set(SDL2_FILTERED_INCLUDE_DIRS "${SDL2_INTERFACE_INCLUDE_DIRS}")
        list(REMOVE_ITEM SDL2_FILTERED_INCLUDE_DIRS
            "C:/msys64/mingw64/include"
            "C:/msys64/mingw64/include/"
            "/mingw64/include"
            "/mingw64/include/"
            "C:/msys64/ucrt64/include"
            "C:/msys64/ucrt64/include/"
            "/ucrt64/include"
            "/ucrt64/include/"
            "C:/msys64/clang64/include"
            "C:/msys64/clang64/include/"
        )
        set_property(TARGET SDL2::SDL2 PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${SDL2_FILTERED_INCLUDE_DIRS}")
    endif()
endif()

option(MYSOFTR_RENDER_DISABLE_DEP_UPDATES
    "Disable FetchContent git update checks during configure"
    OFF)

option(MYSOFTR_RENDER_OFFLINE_DEPS
    "Use only already-populated FetchContent deps (no download/update)"
    OFF)

if(MYSOFTR_RENDER_DISABLE_DEP_UPDATES)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
endif()

if(MYSOFTR_RENDER_OFFLINE_DEPS)
    set(FETCHCONTENT_FULLY_DISCONNECTED ON)
endif()

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://gitee.com/mirrors/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://gitee.com/mirrors/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://gitee.com/mirrors/imgui.git
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
    src/DebugUI.cpp
    src/Light.cpp
    src/ObjLoader.cpp
    src/Object.cpp
    src/RenderThreadPool.cpp
    src/Sphere.cpp
    src/Texture.cpp
    src/Rasterizer.cpp
    src/software_renderer.cpp
)

add_executable(skyboxLutBaker
    utility/SkyboxLutBakerMain.cpp
    utility/SkyboxLutGenerator.cpp
    src/Texture.cpp
)

add_executable(skyboxSpecularLodBaker
    utility/SkyboxSpecularLodBakerMain.cpp
    src/Texture.cpp
)

target_include_directories(mySoftRender PRIVATE include)
target_include_directories(skyboxLutBaker PRIVATE include utility)
target_include_directories(skyboxSpecularLodBaker PRIVATE include utility)

target_link_libraries(mySoftRender PRIVATE
    SDL2::SDL2
    Threads::Threads
    glm::glm
    stb_image_wrapper
    imgui_sdl
)

target_link_libraries(skyboxLutBaker PRIVATE
    glm::glm
    stb_image_wrapper
)

target_link_libraries(skyboxSpecularLodBaker PRIVATE
    glm::glm
    stb_image_wrapper
)

# 添加专门针对软渲染的激进性能优化编译参数
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mySoftRender PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-ffast-math>
    )
    target_compile_options(skyboxLutBaker PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-ffast-math>
    )
    target_compile_options(skyboxSpecularLodBaker PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-ffast-math>
    )
endif()

if(TARGET SDL2::SDL2main)
    target_link_libraries(mySoftRender PRIVATE "$<LINK_ONLY:SDL2::SDL2main>")
endif()

if(MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLLS
        "${MINGW_BIN_DIR}/SDL2.dll"
        "${MINGW_BIN_DIR}/libstdc++-6.dll"
        "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_BIN_DIR}/libwinpthread-1.dll"
    )

    foreach(RUNTIME_DLL IN LISTS MINGW_RUNTIME_DLLS)
        if(EXISTS "${RUNTIME_DLL}")
            add_custom_command(TARGET mySoftRender POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:mySoftRender>"
            )
        endif()
    endforeach()
endif()
