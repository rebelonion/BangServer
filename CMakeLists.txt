cmake_minimum_required(VERSION 3.20)
project(BangServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

# Add Abseil
include(FetchContent)
set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG  20250512.0
)
FetchContent_MakeAvailable(abseil)

# Add toml11
FetchContent_Declare(
        toml11
        GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
        GIT_TAG v4.4.0
)
FetchContent_MakeAvailable(toml11)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mtune=native -fomit-frame-pointer -flto -fno-stack-protector -ffast-math -funroll-loops -finline-functions -fpredictive-commoning -fgcse-after-reload -ftree-vectorize -ftree-partial-pre -fno-semantic-interposition -fno-trapping-math -falign-functions=64 -falign-loops=64 -fno-math-errno -fno-signed-zeros -fexceptions")

    # Prioritize speed over size
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections -Wl,--gc-sections")

    # Optimize for branch prediction
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -freorder-blocks-algorithm=stc -freorder-blocks-and-partition")

    # Enable more aggressive optimizations with GCC
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -floop-nest-optimize -floop-parallelize-all -ftree-loop-distribution -ftree-loop-im -ftree-loop-ivcanon -fivopts")
    endif ()

    # To enable profile-guided optimization
    # Use: 1) Build with -fprofile-generate, 2) Run benchmark, 3) Rebuild with -fprofile-use
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use")
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-generate")
endif ()

add_executable(BangServer
        main.cpp
        src/simdjson.cpp
        src/url_processing.cpp
        src/http_handler.cpp
        src/bang_manager.cpp
        src/bang_provider.cpp
        src/bang_format.cpp
        src/bang_source.cpp
        src/bang_provider_factory.cpp
        src/server_config.cpp
        src/config_utils.cpp
)

add_executable(BangBenchmark
        benchmark.cpp
        src/simdjson.cpp
        src/url_processing.cpp
        src/http_handler.cpp
        src/bang_manager.cpp
        src/bang_provider.cpp
        src/bang_format.cpp
        src/bang_source.cpp
        src/bang_provider_factory.cpp
        src/server_config.cpp
        src/config_utils.cpp
)

target_include_directories(BangServer PRIVATE ${LIBURING_INCLUDE_DIRS})
target_link_libraries(BangServer PRIVATE
        ${LIBURING_LIBRARIES}
        absl::flat_hash_map
        absl::strings
        toml11::toml11
)


target_include_directories(BangBenchmark PRIVATE ${LIBURING_INCLUDE_DIRS})
target_link_libraries(BangBenchmark PRIVATE
        pthread
        ${LIBURING_LIBRARIES}
        absl::flat_hash_map
        absl::strings
        toml11::toml11
)
set_target_properties(BangBenchmark PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
        OUTPUT_NAME bangbenchmark
)


set_target_properties(BangServer PROPERTIES
        OUTPUT_NAME bangserver
        INTERPROCEDURAL_OPTIMIZATION TRUE
)

install(TARGETS BangServer
        RUNTIME DESTINATION bin
)

configure_file(bangs.example.json ${CMAKE_CURRENT_BINARY_DIR}/bangs.json COPYONLY)
configure_file(bangserver.example.toml ${CMAKE_CURRENT_BINARY_DIR}/bangserver.toml COPYONLY)