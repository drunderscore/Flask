cmake_minimum_required(VERSION 3.19)

project(Flask)

set(TF2_PATH "" ON CACHE STRING "Path to the TF2 directory")
set(FLASK_DEVELOPMENT OFF CACHE STRING "Building Flask during development")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (WIN32)
    add_compile_options(/MT)
else()
    add_compile_options(-fno-gnu-unique)
endif ()

include(FetchContent)
set(ABSL_ENABLE_INSTALL ON)

FetchContent_Declare(absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG        20250512.1
)

FetchContent_MakeAvailable(absl)

set(protobuf_BUILD_TESTS OFF)
FetchContent_Declare(protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG        v33.2
)

FetchContent_MakeAvailable(protobuf)
include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)

FetchContent_Declare(boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG        boost-1.89.0
)

FetchContent_MakeAvailable(boost)

# Use breakpad on all platforms
# Linux uses breakpad by default, but Windows uses crashpad by default (in-process vs. out-of-process)
set(SENTRY_BACKEND breakpad)

add_subdirectory(sentry-native)
add_subdirectory(spdlog)
add_subdirectory(JMP)

set(SUBHOOK_TESTS OFF)
set(SUBHOOK_STATIC ON)
add_subdirectory(subhook)

set(PROTO_SOURCE_DIR src/Protocol)
add_library(FlaskNetworkProtocol OBJECT ${PROTO_SOURCE_DIR}/flask.proto)
target_link_libraries(FlaskNetworkProtocol PUBLIC protobuf::libprotobuf)

set(GENERATED_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(PROTO_BINARY_DIR ${GENERATED_BINARY_DIR}/Protocol)

target_include_directories(FlaskNetworkProtocol PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")

protobuf_generate(
        TARGET FlaskNetworkProtocol
        IMPORT_DIRS ${PROTO_SOURCE_DIR}
        PROTOC_OUT_DIR ${PROTO_BINARY_DIR})

include(cmake/GetGitRevisionDescription.cmake)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
string(SUBSTRING ${GIT_SHA1} 0 7 GIT_SHA1_SHORT)

add_library(Flask SHARED
        src/DataTableHelper.cpp
        src/Flask.cpp
        src/TraceFilter.cpp

        src/Network/Client.cpp
        src/Network/WebsocketServer.cpp

        src/Modules/AdditionalPointsOfInterest.cpp
        src/Modules/Camera.cpp
        src/Modules/CameraRig.cpp
        src/Modules/DataTableChangeListener.cpp
        src/Modules/DebugTools.cpp
        src/Modules/EntityEnumerator.cpp
        src/Modules/EntityListener.cpp
        src/Modules/ErrorReporting.cpp
        src/Modules/GameSystem.cpp
        src/Modules/HideRespawnRoomVisualizers.cpp
        src/Modules/HUD.cpp
        src/Modules/Interfaces.cpp
        src/Modules/NetworkCache.cpp
        src/Modules/Passtime.cpp
        src/Modules/Server.cpp

        src/Protocol/Flask.cpp
)

target_compile_definitions(Flask PRIVATE
        CLIENT_DLL
        FLASK_GIT_SHA1="${GIT_SHA1_SHORT}"
        # FIXME: Do we need this?
        PLATFORM_64BITS
        COMPILER_MSVC64
)

if (FLASK_DEVELOPMENT)
    target_compile_definitions(Flask PRIVATE FLASK_DEVELOPMENT)
endif ()

target_include_directories(Flask SYSTEM PRIVATE
        source-sdk-2013/src/common
        source-sdk-2013/src/public
        source-sdk-2013/src/public/engine
        source-sdk-2013/src/public/mathlib
        source-sdk-2013/src/public/vstdlib
        source-sdk-2013/src/public/tier0
        source-sdk-2013/src/public/game/server

        JMP/src

        ${GENERATED_BINARY_DIR}
)

# Technically not Linux, but no one is building this on APPLE
if (UNIX)
    # FIXME: Rebasing/merging alliedmodders/hl2sdk into drunderscore/hl2sdk got us the VScript changes, but it also got us
    #        some changes to utlvector.h. These changes do not compile on Linux without -fpermissive.
    #        On it's own it's not that problematic, but it means that ALL of my code will also be compiled with
    #        -fpermissive, which isn't desirable at all.
    target_compile_options(Flask PRIVATE -fpermissive -iquote ${CMAKE_CURRENT_SOURCE_DIR}/source-sdk-2013/src/public/tier1)

    target_compile_definitions(Flask PRIVATE
            stricmp=strcasecmp
            _stricmp=strcasecmp
            _snprintf=snprintf
            _vsnprintf=vsnprintf
            GNUC
            _LINUX
            LINUX
            POSIX
            USE_SDL
    )

    target_link_directories(Flask PRIVATE source-sdk-2013/src/lib/public/linux64 ${TF2_PATH}/bin/linux64)
    target_link_libraries(Flask PRIVATE -l:tier1.a steam_api -l:mathlib.a)

    set_target_properties(Flask PROPERTIES PREFIX "")
elseif (WIN32)
    target_compile_definitions(Flask PRIVATE WIN32_LEAN_AND_MEAN)
    target_include_directories(Flask PRIVATE source-sdk-2013/src/public/tier1)

    target_link_directories(Flask PRIVATE source-sdk-2013/src/lib/public/win64 ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Windows/x64/)
    target_link_libraries(Flask PRIVATE tier1 legacy_stdio_definitions steam_api64 mathlib)
endif ()

target_link_libraries(Flask PRIVATE tier0 spdlog::spdlog JMP FlaskNetworkProtocol sentry::sentry subhook::subhook Boost::beast)
