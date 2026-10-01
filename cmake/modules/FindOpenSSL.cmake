# Answers find_package(OpenSSL) with the LibreSSL that OpenSSL.cmake fetched.
#
# curl looks its TLS backend up with find_package(OpenSSL), and the stock
# FindOpenSSL only knows how to find an *installed* one. LibreSSL here is a
# FetchContent subproject with no install step, so the stock module either fails
# or -- worse -- succeeds against whatever OpenSSL happens to be on the machine,
# leaving two TLS implementations with the same symbol names in one process.
#
# This shim is only on CMAKE_MODULE_PATH while curl is being added, so nothing
# else in the build has its find_package(OpenSSL) answered by it.

if(NOT TARGET ssl OR NOT TARGET crypto)
    message(FATAL_ERROR
        "FindOpenSSL shim: LibreSSL's ssl/crypto targets do not exist. "
        "cmake/OpenSSL.cmake has to run before anything that looks for OpenSSL.")
endif()

if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto ALIAS crypto)
endif()

if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL ALIAS ssl)
endif()

set(OPENSSL_FOUND TRUE)
set(OPENSSL_INCLUDE_DIR "${libressl_SOURCE_DIR}/include")
set(OPENSSL_INCLUDE_DIRS "${libressl_SOURCE_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY crypto)
set(OPENSSL_SSL_LIBRARY ssl)
set(OPENSSL_LIBRARIES ssl crypto)

# LibreSSL reports itself as OpenSSL 2.0 through OPENSSL_VERSION_NUMBER, which
# is older than every version curl checks against. The number below is what the
# API actually behaves like, not what the header says; curl's LibreSSL support
# keys off LIBRESSL_VERSION_NUMBER in the source, not off this.
set(OPENSSL_VERSION "3.0.0")
