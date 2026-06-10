#ifndef LIBMSE_API_H
#define LIBMSE_API_H

#if defined(_WIN32) || defined(__CYGWIN__) // Windows / Cygwin
    #ifdef LIBMSE_EXPORTS
        #ifdef __GNUC__
            #define LIBMSE_API __attribute__((dllexport))
        #else
            #define LIBMSE_API __declspec(dllexport)
        #endif
    #else
        #ifdef __GNUC__
            #define LIBMSE_API __attribute__((dllimport))
        #else
            #define LIBMSE_API __declspec(dllimport)
        #endif
    #endif
#else // Unix-like (Linux, macOS, BSD)
    #if __GNUC__ >= 4 || defined(__clang__) // GCC 4+ and Clang
        #define LIBMSE_API __attribute__((visibility("default")))
    #else
        #define LIBMSE_API
    #endif
#endif

#endif // LIBMSE_API_H