cmake_minimum_required(VERSION 3.19)

project(Flask)

set(HL2SDK_PATH ${PROJECT_SOURCE_DIR}/hl2sdk)
set(TF2_PATH "" ON CACHE STRING "Path to the TF2 directory")
set(BOOST_INCLUDE_DIRECTORY "C:/Program Files/boost_1_80_0" ON CACHE STRING "Path to the Boost includes")
set(FLASK_DEVELOPMENT OFF CACHE STRING "Building Flask during development")

set(CMAKE_CXX_STANDARD 20)

# Source Engine is 32-bit, so force Lagom to build 32-bit too
if (UNIX)
    add_compile_options(-m32)
    add_link_options(-m32)
elseif (WIN32)
    add_compile_options(/MT)
endif ()

set(protobuf_BUILD_TESTS OFF)

# FIXME: This probably means that Windows will not have zlib support for protobuf
#        but I'm not sure our messages are large enough individually to warrant it
#        at the moment.
if (UNIX)
    set(ZLIB_LIBRARY /usr/lib32/libz.so)
    find_package(ZLIB REQUIRED COMPONENTS)
    set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION_RELEASE ${ZLIB_LIBRARY})
    set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION ${ZLIB_LIBRARY})
endif ()

if (UNIX)
    set(CURL_LIBRARY /usr/lib32/libcurl.so)
    find_package(CURL REQUIRED COMPONENTS AsynchDNS)
    set_target_properties(CURL::libcurl PROPERTIES IMPORTED_LOCATION_RELEASE ${CURL_LIBRARY})
endif ()

# Use breakpad on all platforms
# Linux uses breakpad by default, but Windows uses crashpad by default (in-process vs. out-of-process)
set(SENTRY_BACKEND breakpad)

add_subdirectory(sentry-native)
add_subdirectory(protobuf)
add_subdirectory(spdlog)
add_subdirectory(JMP)

set(SUBHOOK_TESTS OFF)
set(SUBHOOK_STATIC OFF)
add_subdirectory(subhook)

include(protobuf/cmake/protobuf-generate.cmake)

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
        src/Modules/DataTableChangeListener.cpp
        src/Modules/EntityEnumerator.cpp
        src/Modules/EntityListener.cpp
        src/Modules/ErrorReporting.cpp
        src/Modules/GameSystem.cpp
        src/Modules/HideRespawnRoomVisualizers.cpp
        src/Modules/Interfaces.cpp
        src/Modules/NetworkCache.cpp
        src/Modules/Server.cpp

        src/Protocol/Flask.cpp
)

# FIXME: Rebasing/merging alliedmodders/hl2sdk into drunderscore/hl2sdk got us the VScript changes, but it also got us
#        some changes to utlvector.h. These changes do not compile on Linux without -fpermissive.
#        On it's own it's not that problematic, but it means that ALL of my code will also be compiled with
#        -fpermissive, which isn't desirable at all.
if (UNIX)
    target_compile_options(Flask PRIVATE -fpermissive -iquote ${HL2SDK_PATH}/public/tier1)
endif ()

target_compile_definitions(Flask PRIVATE
        CLIENT_DLL
        FLASK_GIT_SHA1="${GIT_SHA1_SHORT}"
)

if (FLASK_DEVELOPMENT)
    target_compile_definitions(Flask PRIVATE FLASK_DEVELOPMENT)
endif ()

target_include_directories(Flask SYSTEM PRIVATE
        ${HL2SDK_PATH}/common
        ${HL2SDK_PATH}/public
        ${HL2SDK_PATH}/public/engine
        ${HL2SDK_PATH}/public/mathlib
        ${HL2SDK_PATH}/public/vstdlib
        ${HL2SDK_PATH}/public/tier0
        ${HL2SDK_PATH}/public/game/server

        JMP/src

        ${BOOST_INCLUDE_DIRECTORY}
        ${GENERATED_BINARY_DIR}
)

# Technically not Linux, but no one is building this on APPLE
if (UNIX)
    target_compile_definitions(Flask PRIVATE
            stricmp=strcasecmp
            _stricmp=strcasecmp
            _snprintf=snprintf
            _vsnprintf=vsnprintf
            GNUC
            _LINUX
            POSIX
            USE_SDL
    )

    # FIXME: So, Source uses tcmalloc by Google to allocate memory. This means calls to malloc() and free() go through
    #        tcmalloc. However, tcmalloc (in this implementation) does _not_ implement or override aligned_alloc(), so
    #        calls to it go through whereever they normally would. This would be sorta okay... if it weren't for the
    #        implicit contract that memory allocated from alligned_alloc() can be freed by free()... tcmalloc can't free
    #        memory it didn't itself allocate!
    #        To remedy, I've placed this modified header file in the include path, which forces Boost to use a different
    #        allocation method... this is garbage.
    target_include_directories(Flask SYSTEM PRIVATE FakeBoostAsio)
elseif (WIN32)
    target_compile_definitions(Flask PRIVATE WIN32_LEAN_AND_MEAN)
    target_include_directories(Flask PRIVATE ${HL2SDK_PATH}/public/tier1)
endif ()

target_link_directories(Flask PRIVATE ${TF2_PATH}/bin)

if (UNIX)
    set_target_properties(Flask PROPERTIES PREFIX "")

    target_link_directories(Flask PRIVATE ${HL2SDK_PATH}/lib/linux)
    target_link_libraries(Flask PRIVATE -l:tier1_i486.a steam_api)
elseif (WIN32)
    target_link_directories(Flask PRIVATE ${HL2SDK_PATH}/lib/public)
    # The steam_api.lib provided by hl2sdk is outdated, and if we provide one it'll conflict with the existing one.
    # So we'll use an absolute path to it on Windows.
    target_link_libraries(Flask PRIVATE tier1 legacy_stdio_definitions ${CMAKE_SOURCE_DIR}/Libraries/Windows/steam_api.lib)
endif ()

target_link_libraries(Flask PRIVATE tier0 spdlog::spdlog JMP FlaskNetworkProtocol sentry::sentry subhook::subhook)
