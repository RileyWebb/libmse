cmake_minimum_required(VERSION 4.1)
project(libmse C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIBMSE_SOURCES
    src/libmse.c
    src/libmse_cvar.c
    src/libmse_cmd.c
    src/libmse_sync.c
    src/libmse_gfx.c
    src/libmse_debug.c
    src/file_registry.c
)

add_library(libmse SHARED ${LIBMSE_SOURCES})
target_include_directories(libmse PUBLIC include)
set_target_properties(libmse PROPERTIES PREFIX "")
target_compile_definitions(libmse PRIVATE LIBMSE_EXPORTS)

target_link_libraries(libmse PRIVATE ${PLATFORM_LIBS})
target_link_libraries(libmse PUBLIC cjson minizip SDL3-shared) #stb)

if(NOT WIN32)
    target_link_libraries(libmse PRIVATE dl)
endif()

if(USE_LUAJIT)
    if(LUA_LIBS)
        target_link_libraries(libmse PUBLIC ${LUA_LIBS})
    endif()
    if(LUA_INCLUDE)
        target_include_directories(libmse PUBLIC ${LUA_INCLUDE})
    endif()
    target_compile_definitions(libmse PUBLIC USE_LUAJIT=1)
    message(STATUS "Lua scripting ENABLED")
else()
    message(STATUS "Lua scripting DISABLED")
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
)