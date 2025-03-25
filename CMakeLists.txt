cmake_minimum_required(VERSION 3.20)
project(BangServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(third_party/abseil-cpp)

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
        src/bang.cpp
        src/simdjson.cpp
        src/url_processing.cpp
)

add_executable(BangBenchmark
        benchmark.cpp
        src/bang.cpp
        src/simdjson.cpp
        src/url_processing.cpp
)

target_include_directories(BangServer PRIVATE ${LIBURING_INCLUDE_DIRS})
target_link_libraries(BangServer PRIVATE
        ${LIBURING_LIBRARIES}
        absl::flat_hash_map
        absl::strings
)


target_include_directories(BangBenchmark PRIVATE ${LIBURING_INCLUDE_DIRS})
target_link_libraries(BangBenchmark PRIVATE
        pthread
        ${LIBURING_LIBRARIES}
        absl::flat_hash_map
        absl::strings
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