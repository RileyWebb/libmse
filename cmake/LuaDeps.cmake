include(FetchContent)

# LuaJIT
set(USE_LUAJIT 0 CACHE BOOL "Enable LuaJIT scripting" FORCE)
set(LUA_LIBS "" CACHE STRING "Lua libraries to link" FORCE)
set(LUA_INCLUDE "" CACHE STRING "Lua include directory" FORCE)
set(LUA_BUILD_EXE 0 CACHE BOOL "Build Lua executable" FORCE)
set(LUAJIT_BUILD_EXE 1 CACHE BOOL "Build LuaJIT executable" FORCE)

FetchContent_Declare(
    luajit_src
    GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG        v2.1
)
FetchContent_Declare(
    luajit_cmake
    GIT_REPOSITORY https://github.com/zhaozg/luajit-cmake.git
    GIT_TAG        master
)

FetchContent_GetProperties(luajit_src)
if(NOT luajit_src_POPULATED)
    FetchContent_MakeAvailable(luajit_src)
endif()

message(STATUS "Building LuaJIT from FetchContent contexts")
set(LUAJIT_DIR "${luajit_src_SOURCE_DIR}" CACHE PATH "LuaJIT source root" FORCE)

# LuaJIT has to be one shared VM for the whole process. luajit-cmake declares
# libluajit without an explicit STATIC/SHARED, so it follows BUILD_SHARED_LIBS;
# left static, every module that links it -- libmse.dll, the frontend, and each
# backend DLL -- gets its own copy of the interpreter. Backends already pass a
# lua_State from libmse's copy to their own copy's lua_* functions (see
# library_meta_handler in the cNES backend), which works only by coincidence.
# Scoped rather than set globally so the other fetched projects keep whatever
# linkage they chose for themselves.
set(MSE_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ON)
FetchContent_MakeAvailable(luajit_cmake)
set(BUILD_SHARED_LIBS ${MSE_SAVED_BUILD_SHARED_LIBS})

if(TARGET libluajit)
    # LuaJIT's headers only mark LUA_API as dllexport when LUA_BUILD_AS_DLL is
    # defined, which luajit-cmake never does, so nothing would be exported.
    set_target_properties(libluajit PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
endif()

if(TARGET minilua)
    set_target_properties(minilua PROPERTIES 
        EXCLUDE_FROM_ALL TRUE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/host_tools"
    )
endif()

if(TARGET buildvm)
    set_target_properties(buildvm PROPERTIES 
        EXCLUDE_FROM_ALL TRUE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/host_tools"
    )
endif()

set(USE_LUAJIT 1 CACHE BOOL "Enable LuaJIT scripting" FORCE)
set(LUA_LIBS luajit::lib CACHE STRING "Lua libraries to link" FORCE)
set(LUA_INCLUDE "${luajit_src_SOURCE_DIR}/src" CACHE STRING "Lua include directory" FORCE)

# inspect.lua
file(DOWNLOAD 
    "https://raw.githubusercontent.com/kikito/inspect.lua/master/inspect.lua"
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/lua/inspect.lua"
    STATUS inspect_status
)

# luasocket
FetchContent_Declare(
    luasocket
    GIT_REPOSITORY https://github.com/lunarmodules/luasocket.git
    GIT_TAG v3.1.0
)

FetchContent_GetProperties(luasocket)
if(NOT luasocket_POPULATED)
    FetchContent_MakeAvailable(luasocket)
endif()

set(LUASOCKET_SRC
    "${luasocket_SOURCE_DIR}/src/luasocket.c"
    "${luasocket_SOURCE_DIR}/src/timeout.c"
    "${luasocket_SOURCE_DIR}/src/buffer.c"
    "${luasocket_SOURCE_DIR}/src/io.c"
    "${luasocket_SOURCE_DIR}/src/auxiliar.c"
    "${luasocket_SOURCE_DIR}/src/options.c"
    "${luasocket_SOURCE_DIR}/src/inet.c"
    "${luasocket_SOURCE_DIR}/src/tcp.c"
    "${luasocket_SOURCE_DIR}/src/udp.c"
    "${luasocket_SOURCE_DIR}/src/except.c"
    "${luasocket_SOURCE_DIR}/src/select.c"
    "${luasocket_SOURCE_DIR}/src/compat.c"
)

if(WIN32)
    list(APPEND LUASOCKET_SRC "${luasocket_SOURCE_DIR}/src/wsocket.c")
else()
    list(APPEND LUASOCKET_SRC "${luasocket_SOURCE_DIR}/src/usocket.c")
endif()

add_library(socket_core MODULE ${LUASOCKET_SRC})

# Lua wants these at socket/core.dll and mime/core.dll, so both targets are
# named "core". Without separate output directories they build to the same
# bin/core.dll and overwrite each other, and both POST_BUILD copies below then
# stage whichever won -- which is why mime/core.dll exported luaopen_socket_core
# and require("mime.core") failed.
set_target_properties(socket_core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "core"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua_modules/socket"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua_modules/socket"
)

if(WIN32)
    target_link_libraries(socket_core PUBLIC libluajit ws2_32)
else()
    target_link_libraries(socket_core PUBLIC libluajit)
endif()

add_library(mime_core MODULE 
    "${luasocket_SOURCE_DIR}/src/mime.c"
    "${luasocket_SOURCE_DIR}/src/compat.c"
)

set_target_properties(mime_core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "core" # Lua requires "mime/core.dll"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua_modules/mime"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua_modules/mime"
)

target_link_libraries(mime_core PUBLIC libluajit)

set(LOCAL_LUA_MODS_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/lua")

add_custom_command(TARGET socket_core POST_BUILD
    COMMENT "Copying LuaSocket binaries and scripts..."
    
    # 1. Create the socket/ subdirectory
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOCAL_LUA_MODS_DIR}/socket"
    
    # 2. Copy the socket/core.dll binary
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:socket_core> "${LOCAL_LUA_MODS_DIR}/socket/"
    
    # 3. Copy the base socket.lua file to the root lua dir
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/socket.lua" "${LOCAL_LUA_MODS_DIR}/"
    
    # 4. Copy the protocol scripts into the socket/ subdirectory
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/ftp.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/http.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/smtp.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/tp.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/url.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/headers.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/ltn12.lua" "${LOCAL_LUA_MODS_DIR}/socket/"
)

add_custom_command(TARGET mime_core POST_BUILD
    COMMENT "Copying LuaSocket Mime binaries and scripts..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOCAL_LUA_MODS_DIR}/mime"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mime_core> "${LOCAL_LUA_MODS_DIR}/mime/core$<TARGET_FILE_SUFFIX:mime_core>"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasocket_SOURCE_DIR}/src/mime.lua" "${LOCAL_LUA_MODS_DIR}/"
)

# luasec
FetchContent_Declare(
    luasec
    GIT_REPOSITORY https://github.com/lunarmodules/luasec.git
    GIT_TAG v1.3.2
)

FetchContent_GetProperties(luasec)
if(NOT luasec_POPULATED)
    FetchContent_MakeAvailable(luasec)
endif()

set(LUASEC_SRC
    "${luasec_SOURCE_DIR}/src/x509.c"
    "${luasec_SOURCE_DIR}/src/context.c"
    "${luasec_SOURCE_DIR}/src/ssl.c"
    "${luasec_SOURCE_DIR}/src/options.c"
    "${luasec_SOURCE_DIR}/src/config.c"
    "${luasec_SOURCE_DIR}/src/ec.c"

    # Internal LuaSocket implementation source files bundled inside LuaSec
    "${luasec_SOURCE_DIR}/src/luasocket/io.c"
    "${luasec_SOURCE_DIR}/src/luasocket/buffer.c"
    "${luasec_SOURCE_DIR}/src/luasocket/timeout.c"
)

# Append platform-specific socket routines from LuaSec's internal layout
if(WIN32)
    list(APPEND LUASEC_SRC "${luasec_SOURCE_DIR}/src/luasocket/wsocket.c")
else()
    list(APPEND LUASEC_SRC "${luasec_SOURCE_DIR}/src/luasocket/usocket.c")
endif()

add_library(ssl_core MODULE ${LUASEC_SRC})

# LibreSSL header paths are automatically generated and passed via target properties
target_include_directories(ssl_core PRIVATE 
    "${LUA_INCLUDE}"
    "${luasec_SOURCE_DIR}/src"
)

set_target_properties(ssl_core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ssl"
)

target_link_libraries(ssl_core PUBLIC 
    libluajit 
    ssl
    crypto
)

if(WIN32)
    target_link_libraries(ssl_core PUBLIC ws2_32 crypt32)
endif()

add_custom_command(TARGET ssl_core POST_BUILD
    COMMENT "Copying LuaSec Secure Socket binaries and scripts..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOCAL_LUA_MODS_DIR}/ssl"
    
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ssl_core> "${LOCAL_LUA_MODS_DIR}/"
    
    COMMAND ${CMAKE_COMMAND} -E copy "${luasec_SOURCE_DIR}/src/ssl.lua" "${LOCAL_LUA_MODS_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${luasec_SOURCE_DIR}/src/https.lua" "${LOCAL_LUA_MODS_DIR}/ssl/"
)

# md5
FetchContent_Declare(
    md5_repo
    GIT_REPOSITORY https://github.com/kikito/md5.lua
    GIT_TAG        master
)

FetchContent_GetProperties(md5_repo)
if(NOT md5_repo_POPULATED)
    FetchContent_MakeAvailable(md5_repo)
endif()

add_custom_target(md5 ALL
    COMMENT "Deploying md5.lua script to runtime directory..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOCAL_LUA_MODS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${md5_repo_SOURCE_DIR}/md5.lua" "${LOCAL_LUA_MODS_DIR}/"
)

# json
FetchContent_Declare(
    json_repo
    GIT_REPOSITORY https://github.com/rxi/json.lua
    GIT_TAG        master
)

FetchContent_GetProperties(json_repo)
if(NOT json_repo_POPULATED)
    FetchContent_MakeAvailable(json_repo)
endif()

add_custom_target(json ALL
    COMMENT "Deploying json.lua script to runtime directory..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOCAL_LUA_MODS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${json_repo_SOURCE_DIR}/json.lua" "${LOCAL_LUA_MODS_DIR}/"
)