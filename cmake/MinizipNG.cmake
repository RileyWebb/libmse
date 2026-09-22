include(FetchContent)

set(MZ_COMPAT ON CACHE BOOL "Enables classic unzip.h / zip.h compatibility layer" FORCE)
set(MZ_ZSTD OFF CACHE BOOL "Disable ZSTD compression" FORCE)
set(MZ_LZMA OFF CACHE BOOL "Disable LZMA compression" FORCE)
set(MZ_BZIP2 OFF CACHE BOOL "Disable BZIP2 compression" FORCE)
set(MZ_OPENSSL OFF CACHE BOOL "Disable OpenSSL dependency" FORCE)

FetchContent_Declare(
    minizip-ng
    GIT_REPOSITORY https://github.com/zlib-ng/minizip-ng.git
    GIT_TAG        4.2.1
)

FetchContent_GetProperties(minizip-ng)
if(NOT minizip-ng_POPULATED)
    FetchContent_MakeAvailable(minizip-ng)
endif()