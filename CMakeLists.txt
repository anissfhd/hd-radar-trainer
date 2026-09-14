cmake_minimum_required(VERSION 3.21)

project(HDPhase1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "HDPhase1 is a Windows-only trainer application.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "hde.exe is 32-bit. Configure this trainer for Win32/x86 "
        "(for example: cmake --preset vs2022-x86).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Pin ImGui so later upstream changes cannot silently break the trainer build.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui)

add_library(imgui_core STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
)

target_include_directories(imgui_core PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(imgui_core PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(imgui_core PRIVATE
    d3d9
    imm32
    shell32
)

# WIN32 selects the GUI subsystem and makes wWinMain the application entry point.
add_executable(HDPhase1 WIN32
    src/main.cpp
    src/trainer_ui.cpp
    src/trainer_ui.h
    src/resource.h
    src/ui_resources.rc
    src/cheat_sequence.cpp
    src/cheat_sequence.h
    src/radar.cpp
    src/radar.h
    src/gameplay_mods.cpp
    src/gameplay_mods.h
    src/weapon_mods.cpp
    src/weapon_mods.h
    src/trainer_process.cpp
    src/trainer_process.h
    src/diagnostics.cpp
    src/diagnostics.h
)

target_include_directories(HDPhase1 PRIVATE src)

target_compile_definitions(HDPhase1 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(HDPhase1 PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(imgui_core PRIVATE /W3 /utf-8)
    target_link_options(HDPhase1 PRIVATE /INCREMENTAL:NO)
endif()

target_link_libraries(HDPhase1 PRIVATE
    imgui_core
    advapi32
    d3d9
    user32
    gdi32
    imm32
    ole32
    windowscodecs
    ws2_32
)

set_target_properties(HDPhase1 PROPERTIES
    OUTPUT_NAME "HDFinalAdvanced"
)

function(add_ai_authority_helper target_name output_name role_definition)
    add_executable(${target_name}
        src/ai_authority_helper.cpp
        src/trainer_process.cpp
        src/trainer_process.h
    )

    target_include_directories(${target_name} PRIVATE src)
    target_compile_definitions(${target_name} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        ${role_definition}
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_link_options(${target_name} PRIVATE /INCREMENTAL:NO)
        # The companion PC receives only this executable, so keep the C++
        # runtime inside it instead of requiring a Visual C++ installation.
        set_property(TARGET ${target_name} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    target_link_libraries(${target_name} PRIVATE
        advapi32
        user32
        ws2_32
    )

    set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${output_name}")
endfunction()

# Both machines must enforce the same ownership decision. The host executable
# makes every enemy local; the client executable makes every enemy remote and
# therefore prevents its C_enemy::Tick AI branch from running there.
add_ai_authority_helper(
    HDAuthorityHost
    "HD_AI_AUTHORITY_HOST"
    HD_AI_AUTHORITY_HOST
)
add_ai_authority_helper(
    HDAuthorityClient
    "HD_AI_AUTHORITY_CLIENT"
    HD_AI_AUTHORITY_CLIENT
)
