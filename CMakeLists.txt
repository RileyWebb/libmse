cmake_minimum_required(VERSION 4.1)
project(libmse C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(cmake/OpenSSL.cmake)
include(cmake/LuaDeps.cmake)
include(cmake/Curl.cmake)
include(cmake/MinizipNG.cmake)
include(cmake/SQLite3.cmake)
include(cmake/STB.cmake)

set(LIBMSE_SOURCES
    src/libmse.c
    src/libmse_cvar.c
    src/libmse_cmd.c
    src/libmse_sync.c
    src/libmse_gfx.c
    src/libmse_debug.c
    src/libmse_log.c
    src/libmse_resource.c
    src/libmse_lua.c
    src/libmse_library.c
    src/libmse_db.c
    src/libmse_thread.c
    src/libmse_profiler.c
)

add_library(libmse SHARED ${LIBMSE_SOURCES})
target_include_directories(libmse PUBLIC include)
set_target_properties(libmse PROPERTIES PREFIX "")
target_compile_definitions(libmse PRIVATE LIBMSE_EXPORTS)

target_include_directories(libmse PUBLIC ${LUA_INCLUDE} ${sqlite3_SOURCE_DIR})
target_link_libraries(libmse PRIVATE ${PLATFORM_LIBS} SQLite3 stb)
target_link_libraries(libmse PUBLIC minizip SDL3-shared libluajit pthread)

if(NOT WIN32)
    target_link_libraries(libmse PRIVATE dl)
endif()

target_compile_options(libmse PRIVATE
    $<$<CONFIG:Debug>:
    #-pedantic
    -Wall
    -Wextra
    -Wconversion
    -Wundef
    #-Werror
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable>
    #$<$<CONFIG:RelWithDebInfo>:OPTIMIZED_DEBUG>
)

add_custom_target(libmse_copy_data
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/data/
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/)

add_dependencies(libmse libmse_copy_data socket_core mime_core lcurl_core)

# The cvar system's declared half has no visible failure mode: a declaration
# that never queues, a flush that never repoints, and a cvar that was never
# written all look identical from the outside. This exercises each of them.
add_executable(libmse_cvar_test tests/cvar_test.c)
target_link_libraries(libmse_cvar_test PRIVATE libmse)

add_test(NAME libmse_cvars COMMAND libmse_cvar_test)
set_tests_properties(libmse_cvars PROPERTIES
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    TIMEOUT 30
    LABELS "libmse;cvar")
