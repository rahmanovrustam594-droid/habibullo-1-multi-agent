cmake_minimum_required(VERSION 3.16)

project(HabibulloUltimateCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Try a local install first, otherwise fetch llama.cpp automatically.
find_package(llama CONFIG QUIET)

if(NOT llama_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        llama
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(llama)
endif()

add_executable(habibullo main.cpp)

target_link_libraries(habibullo PRIVATE llama)

target_compile_definitions(habibullo PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

message(STATUS "HabibulloUltimateCore configured successfully.")
