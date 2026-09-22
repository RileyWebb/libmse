include(FetchContent)

# LibreSSL
set(LIBRESSL_APPS OFF CACHE BOOL "Disable LibreSSL applications build" FORCE)
set(LIBRESSL_TESTS OFF CACHE BOOL "Disable LibreSSL testing suites" FORCE)

FetchContent_Declare(
    libressl
    URL https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-4.3.2.tar.gz
)

FetchContent_GetProperties(libressl)
if(NOT libressl_POPULATED)
    FetchContent_MakeAvailable(libressl)
endif()