cmake_minimum_required(VERSION 3.20)
project(UniversalOffsetDumper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# JSON
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# Zydis
FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG        v4.0.0
)
FetchContent_MakeAvailable(zydis)

# Add include directories
include_directories(include)
include_directories(${imgui_SOURCE_DIR})
include_directories(${imgui_SOURCE_DIR}/backends)

# Source files - Filter out tests
file(GLOB_RECURSE SOURCES "src/*.cpp")
# Include ImGui implementation files
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES} ${IMGUI_SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE Zydis)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -mavx2)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 d3dcompiler dwmapi setupapi)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS)
    else()
        target_link_options(${PROJECT_NAME} PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
        # Ensure static linking for MinGW
        set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-static -static-libgcc -static-libstdc++")
    endif()
endif()

# Tests
add_executable(UnitTests tests/Tests.cpp src/Core/ProcessManager.cpp src/Core/MemoryScanner.cpp src/Core/Scanner.cpp src/Core/OffsetResolver.cpp src/Core/PointerScanner.cpp)
target_link_libraries(UnitTests PRIVATE nlohmann_json::nlohmann_json Zydis)
target_compile_options(UnitTests PRIVATE -mavx2)

add_executable(LinuxIntegrationTest tests/LinuxIntegrationTest.cpp src/Core/ProcessManager.cpp src/Core/MemoryScanner.cpp src/Core/Scanner.cpp src/Core/OffsetResolver.cpp src/Core/PointerScanner.cpp)
target_link_libraries(LinuxIntegrationTest PRIVATE nlohmann_json::nlohmann_json Zydis)
target_compile_options(LinuxIntegrationTest PRIVATE -mavx2)

add_executable(PointerScanTest tests/PointerScanTest.cpp src/Core/ProcessManager.cpp src/Core/MemoryScanner.cpp src/Core/Scanner.cpp src/Core/OffsetResolver.cpp src/Core/PointerScanner.cpp)
target_link_libraries(PointerScanTest PRIVATE nlohmann_json::nlohmann_json Zydis)
target_compile_options(PointerScanTest PRIVATE -mavx2)
