include(FetchContent)

# libcurl, plus the Lua-cURLv3 binding on top of it.
#
# luasocket and luasec are still here and still work, but they are a socket
# library with HTTP bolted on: no redirect following, no connection reuse, no
# timeouts worth the name, and HTTPS only through a separate module that has to
# be threaded through by hand. The cover-art and metadata scrapers are the ones
# paying for that, so they get a real HTTP client.
#
# Requires cmake/OpenSSL.cmake (LibreSSL) and cmake/LuaDeps.cmake (libluajit,
# LUA_INCLUDE) to have run first.

set(LOCAL_LUA_MODS_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/lua")

# --- libcurl -----------------------------------------------------------------

set(BUILD_CURL_EXE      OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS   OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS  OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS     OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL  OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL    ON  CACHE BOOL "" FORCE)

# Everything curl would otherwise go looking for on the host. Left to their
# defaults these are "find it if it is there", which makes the build depend on
# what happens to be installed -- a libcurl that links libpsl on one machine and
# not on another is a bug report waiting to be filed against the wrong thing.
set(CURL_USE_LIBPSL     OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2    OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH     OFF CACHE BOOL "" FORCE)
set(CURL_USE_GSSAPI     OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2         OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2         OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB           OFF CACHE STRING "" FORCE)
set(CURL_BROTLI         OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD           OFF CACHE BOOL "" FORCE)

# Protocols nothing here will ever speak. LDAP in particular drags in wldap32
# on Windows for no reason at all.
set(CURL_DISABLE_LDAP    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS   ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER  ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMB     ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP    ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET  ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP    ON CACHE BOOL "" FORCE)

# The trust store. LibreSSL ships the Mozilla bundle in its source tree, so the
# one curl verifies against is the one the rest of the build already trusts
# rather than whatever the host happens to have. Baked in as an absolute path
# because that is the only way libcurl takes a default -- a relocated build has
# to set CURLOPT_CAINFO itself, which is what cURL.lua's wrapper does.
set(MSE_CA_BUNDLE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/cert.pem")
set(CURL_CA_BUNDLE "${MSE_CA_BUNDLE}" CACHE STRING "" FORCE)
set(CURL_CA_PATH   "none" CACHE STRING "" FORCE)

# Scoped exactly like the LuaJIT block in LuaDeps.cmake: shared so there is one
# libcurl in the process no matter how many things end up linking it, without
# changing the linkage the other fetched projects picked for themselves.
set(MSE_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ON)

# The shim that answers curl's find_package(OpenSSL) with LibreSSL. Prepended so
# it wins over the stock module, and taken back off straight after so no other
# subproject is affected by it.
set(MSE_SAVED_MODULE_PATH "${CMAKE_MODULE_PATH}")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_11_1
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(curl)
if(NOT curl_POPULATED)
    FetchContent_MakeAvailable(curl)
endif()

set(CMAKE_MODULE_PATH "${MSE_SAVED_MODULE_PATH}")
set(BUILD_SHARED_LIBS ${MSE_SAVED_BUILD_SHARED_LIBS})

if(TARGET libcurl_shared)
    set_target_properties(libcurl_shared PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
endif()

add_custom_target(curl_ca_bundle ALL
    COMMENT "Deploying CA bundle for libcurl..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libressl_SOURCE_DIR}/cert.pem" "${MSE_CA_BUNDLE}"
)

# --- Lua-cURLv3 --------------------------------------------------------------

# SOURCE_SUBDIR points at a directory with no CMakeLists.txt in it, which is how
# FetchContent is asked to fetch a project without building it. The upstream
# CMakeLists expects an installed libcurl and an installed Lua and neither is
# true here; the source list is short and stable, so the module is built the
# same way socket_core and mime_core are in LuaDeps.cmake.
FetchContent_Declare(
    lua_curl
    GIT_REPOSITORY https://github.com/Lua-cURL/Lua-cURLv3.git
    GIT_TAG        v0.3.13
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  src
)

FetchContent_MakeAvailable(lua_curl)
set(LUA_CURL_SRC
    "${lua_curl_SOURCE_DIR}/src/l52util.c"
    "${lua_curl_SOURCE_DIR}/src/lceasy.c"
    "${lua_curl_SOURCE_DIR}/src/lcerror.c"
    "${lua_curl_SOURCE_DIR}/src/lchttppost.c"
    "${lua_curl_SOURCE_DIR}/src/lcmime.c"
    "${lua_curl_SOURCE_DIR}/src/lcmulti.c"
    "${lua_curl_SOURCE_DIR}/src/lcshare.c"
    "${lua_curl_SOURCE_DIR}/src/lcurl.c"
    "${lua_curl_SOURCE_DIR}/src/lcurlapi.c"
    "${lua_curl_SOURCE_DIR}/src/lcutils.c"
)

add_library(lcurl_core MODULE ${LUA_CURL_SRC})

target_include_directories(lcurl_core PRIVATE
    "${LUA_INCLUDE}"
    "${lua_curl_SOURCE_DIR}/src"
)

# require("lcurl") looks for lcurl.dll, so the module cannot keep the "lib"
# prefix CMake would otherwise give it.
set_target_properties(lcurl_core PROPERTIES
    PREFIX ""
    OUTPUT_NAME "lcurl"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua_modules/lcurl"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lua_modules/lcurl"
)

target_link_libraries(lcurl_core PUBLIC libluajit CURL::libcurl)

add_dependencies(lcurl_core curl_ca_bundle)

add_custom_command(TARGET lcurl_core POST_BUILD
    COMMENT "Copying Lua-cURL binaries and scripts..."
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOCAL_LUA_MODS_DIR}/cURL/impl"

    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lcurl_core> "${LOCAL_LUA_MODS_DIR}/"

    COMMAND ${CMAKE_COMMAND} -E copy
            "${lua_curl_SOURCE_DIR}/src/lua/cURL.lua" "${LOCAL_LUA_MODS_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${lua_curl_SOURCE_DIR}/src/lua/cURL/safe.lua" "${LOCAL_LUA_MODS_DIR}/cURL/"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${lua_curl_SOURCE_DIR}/src/lua/cURL/utils.lua" "${LOCAL_LUA_MODS_DIR}/cURL/"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${lua_curl_SOURCE_DIR}/src/lua/cURL/impl/cURL.lua" "${LOCAL_LUA_MODS_DIR}/cURL/impl/"
)
