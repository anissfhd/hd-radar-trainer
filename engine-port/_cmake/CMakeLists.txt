cmake_minimum_required(VERSION 3.25)
project(HDDeluxeAuthority LANGUAGES C CXX RC)

# Generated from the original VC6 workspace. Do not edit by hand.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
add_compile_definitions(WIN32 NDEBUG HD_CMAKE_PORT)
# VC6 headers inject library names with #pragma comment(lib,...).
# CMake expresses those dependencies as targets; suppress the stale
# copied-library names so link order and locations stay deterministic.
add_link_options(/NODEFAULTLIB:base_lib /NODEFAULTLIB:base_lib_d /NODEFAULTLIB:icom_rd /NODEFAULTLIB:iexcpt /NODEFAULTLIB:insanity /NODEFAULTLIB:insanity_d /NODEFAULTLIB:i3d2_math /NODEFAULTLIB:i3d2_math_d /NODEFAULTLIB:i3d2_thunk /NODEFAULTLIB:igraph2_thunk /NODEFAULTLIB:inet2_thunk /NODEFAULTLIB:isound2_thunk /NODEFAULTLIB:itabler2 /NODEFAULTLIB:ogg /NODEFAULTLIB:vorbis /NODEFAULTLIB:D3dx8 /NODEFAULTLIB:th32)

set(HD_SOURCE_ROOT "C:/Users/user/Desktop/HD_Radar_Project/source/hde")
# Minimal, local DirectX 8 SDK headers/import libraries for this source port.
# They are kept in _vendor and are never copied into the installed game.
set(HD_LEGACY_DXSDK_ROOT "${HD_SOURCE_ROOT}/_vendor/directx8_min" CACHE PATH "Local DirectX 8 compatibility SDK root")
if(EXISTS "${HD_LEGACY_DXSDK_ROOT}/include/dplay8.h")
  message(STATUS "Using local DirectX 8 compatibility SDK: ${HD_LEGACY_DXSDK_ROOT}")
  include_directories("${HD_LEGACY_DXSDK_ROOT}/include")
  if(EXISTS "${HD_LEGACY_DXSDK_ROOT}/lib")
    link_directories("${HD_LEGACY_DXSDK_ROOT}/lib")
  endif()
else()
  message(WARNING "DirectPlay headers absent. hd_inet2 cannot be built without the local compatibility SDK.")
endif()
include_directories("${HD_SOURCE_ROOT}/_src" "${HD_SOURCE_ROOT}/_src/Insanity/Include" "${HD_SOURCE_ROOT}/_src/Insanity/Lib/ISnd2/OggVorbis/include")

# Crash
set(hd_crash_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IExcept/Crash.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IExcept/crash.rc"
)
add_library(hd_crash SHARED ${hd_crash_sources})
target_compile_options(hd_crash PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_crash PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL CRASH_EXPORTS)
target_link_libraries(hd_crash PRIVATE dbghelp)
target_link_libraries(hd_crash PRIVATE hd_base_lib hd_dta_read)

# DebugMem
set(hd_debugmem_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/MemDebDll.cpp"
)
add_library(hd_debugmem SHARED ${hd_debugmem_sources})
target_compile_options(hd_debugmem PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_debugmem PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL DEBUGMEM_EXPORTS)
target_link_libraries(hd_debugmem PRIVATE hd_crash)

# Hidden And Dangerous
set(hd_hidden_and_dangerous_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Actors.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Actors1.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Detector.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Door.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Explosion.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Flare.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Grenade.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/GunHole.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/GunShoot.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/KillAnim.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Ladder.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Mine.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Particle.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/RandomGen.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Resource.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Rocket.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/SmokeRay.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/TimeBomb.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Vehicle.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/BugReport.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/CP_build.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Miss_plg.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Systables.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/CommandLine.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/GameCam.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/GameMenu.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Inventory.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/MainMenu.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Menu.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Console.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Hint.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/I3d_font.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/IconInfo.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/WideText.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Briefing.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/CampAnim.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/GameMission.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Map_man.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Mission.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/TheEnd.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Net.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/NetChat.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/NetSync.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Checkpoint.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Help.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/InitSystem.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Main.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/ScrollText.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Winstuff.cpp"
)
add_executable(hd_hidden_and_dangerous WIN32 ${hd_hidden_and_dangerous_sources})
target_compile_options(hd_hidden_and_dangerous PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_hidden_and_dangerous PRIVATE NDEBUG FINAL BETAVERSION EDITOR)
set_source_files_properties("C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Actors.cpp" PROPERTIES COMPILE_OPTIONS "/Od")
set_source_files_properties("C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/MainMenu.cpp" PROPERTIES COMPILE_OPTIONS "/UEDITOR")
target_link_libraries(hd_hidden_and_dangerous PRIVATE hd_base_lib hd_crash hd_debugmem hd_dta_read hd_i3d_2 hd_i3d_math hd_igraph2 hd_inet2 hd_insanity hd_isnd2 hd_tabler2 hd_i3d2_thunk hd_igraph_thunk hd_inet_thunk hd_isound_thunk)

# I3D2_Thunk
set(hd_i3d2_thunk_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Thunk/Main.cpp"
)
add_library(hd_i3d2_thunk STATIC ${hd_i3d2_thunk_sources})
target_compile_options(hd_i3d2_thunk PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_i3d2_thunk PRIVATE WIN32 NDEBUG _MBCS _LIB)

# I3D_math
set(hd_i3d_math_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/I3D_math/Math_3d.cpp"
)
add_library(hd_i3d_math STATIC ${hd_i3d_math_sources})
target_compile_options(hd_i3d_math PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_i3d_math PRIVATE WIN32 NDEBUG _MBCS _LIB)

# IEditor
set(hd_ieditor_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Access.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/BspTool.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Common.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Create.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/DebugLine.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Editor.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Exit.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Gravitation.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Grid.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Help.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Hide.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/i3d_font.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Light.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/LightMap.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Log.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Modify.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/MouseEdit.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Properties.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Screen.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/ScreenShot.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Seed.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Selection.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Stats.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/SubEdit.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Undo.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Validity.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/VideoGrab.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/View.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IEditor/Insanity.rc"
)
add_library(hd_ieditor SHARED ${hd_ieditor_sources})
target_compile_options(hd_ieditor PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_ieditor PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL IEditor_EXPORTS NO_STARTUP)
set_target_properties(hd_ieditor PROPERTIES OUTPUT_NAME "IEditor")
target_link_options(hd_ieditor PRIVATE /NODEFAULTLIB:IEditor.lib)
target_link_libraries(hd_ieditor PRIVATE comctl32 shell32)
target_link_libraries(hd_ieditor PRIVATE hd_base_lib hd_dta_read hd_tabler2 hd_i3d_math)

# IGraph_Thunk
set(hd_igraph_thunk_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Thunk/Main.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Thunk/Startup.cpp"
)
add_library(hd_igraph_thunk STATIC ${hd_igraph_thunk_sources})
target_compile_options(hd_igraph_thunk PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_igraph_thunk PRIVATE WIN32 NDEBUG _MBCS _LIB)

# INet_thunk
set(hd_inet_thunk_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/Thunk/Main.cpp"
)
add_library(hd_inet_thunk STATIC ${hd_inet_thunk_sources})
target_compile_options(hd_inet_thunk PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_inet_thunk PRIVATE WIN32 NDEBUG _MBCS _LIB)

# ISound_Thunk
set(hd_isound_thunk_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/Thunk/Main.cpp"
)
add_library(hd_isound_thunk STATIC ${hd_isound_thunk_sources})
target_compile_options(hd_isound_thunk PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_isound_thunk PRIVATE WIN32 NDEBUG _MBCS _LIB)

# Insanity
set(hd_insanity_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Insanity/Controller.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Insanity/MessageBox.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Insanity/SourceControl.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Insanity/System.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Insanity/Texts.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Insanity/WinShell.cpp"
)
add_library(hd_insanity STATIC ${hd_insanity_sources})
target_compile_options(hd_insanity PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_insanity PRIVATE WIN32 NDEBUG _MBCS _LIB)

# PNG
set(hd_png_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/png.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngerror.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pnggccrd.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngget.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngmem.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngpread.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngread.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngrio.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngrtran.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngrutil.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngset.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngtrans.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngvcrd.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngwio.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngwrite.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngwtran.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/PNG/pngwutil.c"
)
add_library(hd_png STATIC ${hd_png_sources})
target_compile_options(hd_png PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_png PRIVATE WIN32 NDEBUG _WINDOWS)

# QHull
set(hd_qhull_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/GEOM.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/GEOM2.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/GLOBAL.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/IO.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/MEM.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/MERGE.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/POLY.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/POLY2.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/QHULL.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/QSET.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/STAT.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/Thunk.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/USER.C"
)
set_source_files_properties(
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/GEOM.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/GEOM2.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/GLOBAL.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/IO.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/MEM.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/MERGE.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/POLY.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/POLY2.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/QHULL.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/QSET.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/STAT.C"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/QHull/USER.C"
  PROPERTIES LANGUAGE C)
add_library(hd_qhull STATIC ${hd_qhull_sources})
target_compile_options(hd_qhull PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_qhull PRIVATE WIN32 NDEBUG _MBCS _LIB)

# ZLib
set(hd_zlib_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/adler32.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/compress.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/crc32.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/deflate.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/gzio.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/infblock.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/infcodes.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/inffast.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/inflate.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/inftrees.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/infutil.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/trees.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/uncompr.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ZLib/zutil.c"
)
add_library(hd_zlib STATIC ${hd_zlib_sources})
target_compile_options(hd_zlib PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_zlib PRIVATE WIN32 NDEBUG _WINDOWS)

# base_lib
set(hd_base_lib_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/Beep.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/CommandLine.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/DebugLed.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/Filesize.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/Load.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/MemDebug.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/Profile.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/RadixSort.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/Save.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Base_lib/Win_reg.cpp"
)
add_library(hd_base_lib STATIC ${hd_base_lib_sources})
target_compile_options(hd_base_lib PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_base_lib PRIVATE WIN32 NDEBUG _MBCS _LIB)

# dta_read
set(hd_dta_read_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Dta_read/Dta_read.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Dta_read/Main.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Dta_read/dta.rc"
)
add_library(hd_dta_read SHARED ${hd_dta_read_sources})
target_compile_options(hd_dta_read PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_dta_read PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL MSVC_EXPORTS)
target_compile_definitions(hd_dta_read PRIVATE DTA_READ_BUILD)
set_target_properties(hd_dta_read PROPERTIES OUTPUT_NAME "icom_rd")

# i3d_2
set(hd_i3d_2_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Camera.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Dummy.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Frame.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Joint.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Light.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Model.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/occluder.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Sector.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Sound.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Target.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/User.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Volume.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/billbrd.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/CameraView.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Flare.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Lmap.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Mirror.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Morph.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Object3d.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Object_uvshift.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Particle.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Sng_mesh.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Visual.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Visual_dynamic.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/CPU.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/D3d_err.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/DataBase.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Main.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Anim_3d.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/bsp.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Collision.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Driver.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Load_3ds.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Material.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Mesh.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Procedural.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Render.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Scene.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/Texture.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/I3D2/I3D2.rc"
)
add_library(hd_i3d_2 SHARED ${hd_i3d_2_sources})
target_compile_options(hd_i3d_2 PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_i3d_2 PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL I3D_2_EXPORTS)
target_link_libraries(hd_i3d_2 PRIVATE hd_base_lib hd_qhull hd_dta_read hd_igraph_thunk hd_i3d_math)

# igraph2
set(hd_igraph2_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Conv_rgb.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/D3d_err.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Getascii.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Igraph.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Iimage.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Jpg_load/Jpg_load.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Load_bmp.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Load_jpg.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Load_pcx.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/Load_png.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/SavePNG.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/IGraph2/SaveShot.cpp"
)
add_library(hd_igraph2 SHARED ${hd_igraph2_sources})
target_compile_options(hd_igraph2 PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_igraph2 PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL IGRAPH2_EXPORTS)
target_link_libraries(hd_igraph2 PRIVATE dinput8 dxguid winmm)
target_link_libraries(hd_igraph2 PRIVATE hd_zlib hd_png hd_base_lib hd_dta_read hd_igraph_thunk)

# inet2
set(hd_inet2_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/Dp_err.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/Inet.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/Main.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/Messages.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/Main.rc"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/INet2/LegacyDxGuids.cpp"
)
add_library(hd_inet2 SHARED ${hd_inet2_sources})
target_compile_options(hd_inet2 PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_inet2 PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL INET2_EXPORTS)
target_link_libraries(hd_inet2 PRIVATE dplayx dxguid ole32 user32 winmm gdi32 hd_insanity)
target_link_libraries(hd_inet2 PRIVATE hd_inet_thunk hd_i3d_math hd_base_lib hd_crash)

# isnd2
set(hd_isnd2_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/Isound2.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/LoadOgg.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/Loadwave.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/Main.cpp"
)
add_library(hd_isnd2 SHARED ${hd_isnd2_sources})
target_compile_options(hd_isnd2 PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_isnd2 PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL ISND2_EXPORTS)
target_link_libraries(hd_isnd2 PRIVATE dsound dxguid winmm)
target_link_libraries(hd_isnd2 PRIVATE hd_ogg hd_vorbis hd_dta_read hd_base_lib)

# ogg
set(hd_ogg_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/ogg/bitwise.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/ogg/DebugAlloc.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/ogg/framing.c"
)
add_library(hd_ogg STATIC ${hd_ogg_sources})
target_compile_options(hd_ogg PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_ogg PRIVATE WIN32 NDEBUG _WINDOWS)

# tabler2
set(hd_tabler2_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Tabler2/Compress.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Tabler2/ITabCore.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Tabler2/Main.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Tabler2/Tabbler.cpp"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/Tabler2/Main.rc"
)
add_library(hd_tabler2 SHARED ${hd_tabler2_sources})
target_compile_options(hd_tabler2 PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_tabler2 PRIVATE WIN32 NDEBUG _WINDOWS _MBCS _USRDLL TABLER2_EXPORTS TABBLER_EDIT_SUPPORT)
target_link_libraries(hd_tabler2 PRIVATE comctl32)
target_link_libraries(hd_tabler2 PRIVATE hd_base_lib hd_dta_read)

# vorbis
set(hd_vorbis_sources
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/analysis.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/barkmel.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/bitrate.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/block.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/codebook.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/envelope.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/floor0.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/floor1.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/info.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/lookup.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/lpc.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/lsp.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/mapping0.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/mdct.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/psy.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/registry.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/res0.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/sharedbook.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/smallft.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/synthesis.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/time0.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/vorbisenc.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/vorbisfile.c"
  "C:/Users/user/Desktop/HD_Radar_Project/source/hde/_src/Insanity/Lib/ISnd2/OggVorbis/lib/vorbis/window.c"
)
add_library(hd_vorbis STATIC ${hd_vorbis_sources})
target_compile_options(hd_vorbis PRIVATE /W3 /GR /permissive /Zc:forScope- /Zc:sizedDealloc-)
target_compile_definitions(hd_vorbis PRIVATE WIN32 NDEBUG _WINDOWS)

target_link_libraries(hd_hidden_and_dangerous PRIVATE hd_ieditor)
target_link_libraries(hd_hidden_and_dangerous PRIVATE user32 gdi32 shell32 comdlg32 ole32 oleaut32 winmm dxguid wsock32)
set_target_properties(hd_hidden_and_dangerous PROPERTIES OUTPUT_NAME "HDE_Authority" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
