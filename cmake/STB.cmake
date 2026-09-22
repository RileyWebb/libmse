include(FetchContent)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master # Note: Use a specific commit hash for production stability
)

FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_MakeAvailable(stb)
    
    add_library(stb INTERFACE)
    
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()