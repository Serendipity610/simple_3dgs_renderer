cmake_minimum_required(VERSION 3.20)

project(simple_3dgs_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "T1 currently provides the Win32 window backend only")
endif()

find_package(Vulkan REQUIRED)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SHADER_SOURCES
    shaders/splat.vert
    shaders/splat.frag
    shaders/sort_free_splat.vert
    shaders/sort_free_splat.frag
    shaders/normalize.vert
    shaders/normalize.frag
)
set(SHADER_BINARIES)
foreach(SHADER_SOURCE IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_BINARY "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
                -o "${SHADER_BINARY}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
        VERBATIM
    )
    list(APPEND SHADER_BINARIES "${SHADER_BINARY}")
endforeach()
add_custom_target(simple_3dgs_shaders DEPENDS ${SHADER_BINARIES})

add_library(simple_3dgs_core STATIC
    src/camera.cpp
    src/ply_loader.cpp
)

target_include_directories(simple_3dgs_core PUBLIC include)

if(MSVC)
    target_compile_options(simple_3dgs_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(simple_3dgs_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(simple_3dgs_vulkan STATIC
    src/gaussian_gpu_buffer.cpp
)
target_include_directories(simple_3dgs_vulkan PUBLIC include)
target_link_libraries(simple_3dgs_vulkan PUBLIC Vulkan::Vulkan)
if(MSVC)
    target_compile_options(simple_3dgs_vulkan PRIVATE /W4 /permissive-)
else()
    target_compile_options(simple_3dgs_vulkan PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(simple_3dgs_engine
    src/main.cpp
    src/vulkan_application.cpp
)
set_target_properties(simple_3dgs_engine PROPERTIES WIN32_EXECUTABLE FALSE)

target_compile_definitions(simple_3dgs_engine PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    VK_USE_PLATFORM_WIN32_KHR
)

target_link_libraries(simple_3dgs_engine PRIVATE Vulkan::Vulkan)
target_link_libraries(simple_3dgs_engine PRIVATE
    simple_3dgs_core
    simple_3dgs_vulkan
    shell32
)
add_dependencies(simple_3dgs_engine simple_3dgs_shaders)
add_custom_command(TARGET simple_3dgs_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:simple_3dgs_engine>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_BINARIES}
            "$<TARGET_FILE_DIR:simple_3dgs_engine>/shaders"
)

if(MSVC)
    target_compile_options(simple_3dgs_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(simple_3dgs_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME executable_exists
        COMMAND ${CMAKE_COMMAND} -E compare_files
                $<TARGET_FILE:simple_3dgs_engine>
                $<TARGET_FILE:simple_3dgs_engine>
    )

    add_executable(ply_loader_tests tests/ply_loader_tests.cpp)
    target_link_libraries(ply_loader_tests PRIVATE simple_3dgs_core)
    target_compile_definitions(ply_loader_tests PRIVATE
        SIMPLE_3DGS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    )
    if(MSVC)
        target_compile_options(ply_loader_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ply_loader_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ply_loader COMMAND ply_loader_tests)

    add_executable(ply_load_benchmark tests/ply_load_benchmark.cpp)
    target_link_libraries(ply_load_benchmark PRIVATE simple_3dgs_core)
    if(MSVC)
        target_compile_options(ply_load_benchmark PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ply_load_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(camera_tests tests/camera_tests.cpp)
    target_link_libraries(camera_tests PRIVATE simple_3dgs_core)
    if(MSVC)
        target_compile_options(camera_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(camera_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME camera COMMAND camera_tests)

    add_executable(gaussian_gpu_buffer_tests tests/gaussian_gpu_buffer_tests.cpp)
    target_link_libraries(gaussian_gpu_buffer_tests PRIVATE simple_3dgs_vulkan)
    if(MSVC)
        target_compile_options(gaussian_gpu_buffer_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(gaussian_gpu_buffer_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gaussian_gpu_buffer COMMAND gaussian_gpu_buffer_tests)

    add_test(NAME renderer_smoke COMMAND simple_3dgs_engine --smoke-test)
    set_tests_properties(renderer_smoke PROPERTIES TIMEOUT 15)

    add_test(NAME renderer_ply_smoke
        COMMAND simple_3dgs_engine --smoke-test
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gaussians_ascii.ply"
    )
    set_tests_properties(renderer_ply_smoke PROPERTIES TIMEOUT 15)

    add_test(NAME renderer_sort_free_default_smoke
        COMMAND simple_3dgs_engine --smoke-test
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gaussians_sort_free_ascii.ply"
    )
    set_tests_properties(renderer_sort_free_default_smoke PROPERTIES TIMEOUT 15)

    add_test(NAME renderer_sort_free_smoke
        COMMAND simple_3dgs_engine --smoke-test --sort-free
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gaussians_sort_free_ascii.ply"
    )
    set_tests_properties(renderer_sort_free_smoke PROPERTIES TIMEOUT 15)

    add_test(NAME renderer_sort_free_fallback_smoke
        COMMAND simple_3dgs_engine --smoke-test --sort-free
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gaussians_ascii.ply"
    )
    set_tests_properties(renderer_sort_free_fallback_smoke PROPERTIES TIMEOUT 15)
endif()
