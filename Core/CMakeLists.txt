# i stands for Interface
add_library(corei_gameengine_include INTERFACE)
add_library(corei_libraries_include INTERFACE)
add_library(corei_libraries_source_wwvegas INTERFACE)
add_library(corei_main INTERFACE)
add_library(corei_always INTERFACE)
add_library(corei_always_no_pch INTERFACE) # Use this for Shared Libs with MFC AFX

# Create d3d8lib INTERFACE target early so Core/GameEngine can link to it
# This will be configured later by GeneralsMD/Code/CompatLib/CMakeLists.txt
# Only create if not already created by min-dx8-sdk CMakeLists
if(NOT TARGET d3d8lib)
    add_library(d3d8lib INTERFACE)
endif()

target_include_directories(corei_gameengine_include INTERFACE "GameEngine/Include")
target_include_directories(corei_libraries_include INTERFACE "Libraries/Include")
target_include_directories(corei_libraries_source_wwvegas INTERFACE "Libraries/Source/WWVegas")
target_include_directories(corei_main INTERFACE "Main")

target_sources(corei_libraries_include PRIVATE
    Libraries/Include/Lib/BaseType.h
    Libraries/Include/Lib/BaseTypeCore.h
    Libraries/Include/Lib/trig.h
    Libraries/Include/rts/debug.h
    Libraries/Include/rts/profile.h
)
# GeneralsX @build 12/08/2026 Many sources still include WWLib headers unqualified
# ("win.h", "PchCompat.h", "registryini.h", ...). On mac/linux those resolve through
# CompatLib's include dirs (attached to the game-level *_always targets for NOT WIN32),
# which native Windows builds exclude; expose WWLib here the same way so the MinGW
# build resolves them identically across all targets.
if(WIN32)
    target_include_directories(corei_always INTERFACE "Libraries/Source/WWVegas/WWLib")
    target_include_directories(corei_always_no_pch INTERFACE "Libraries/Source/WWVegas/WWLib")
endif()

target_link_libraries(corei_always INTERFACE
    core_config
    core_profile_tracy
    core_utility
    corei_libraries_include
    resources
)
target_link_libraries(corei_always_no_pch INTERFACE
    core_config
    core_utility_no_pch
    corei_libraries_include
    resources
)

# Set where the build results will end up
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Contains internal libraries
add_subdirectory(Libraries)

# Base GameEngine library
add_subdirectory(GameEngine)

# Platform specific GameEngine code
add_subdirectory(GameEngineDevice)

add_subdirectory(Tools)
