cmake_minimum_required(VERSION 3.26)

project(EngineTest VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENGINE_BUILD_SANDBOX "Build the sandbox executable" ON)
option(ENGINE_BUILD_TESTS "Build unit and integration tests" ON)
option(ENGINE_BUILD_HUMAN_DEVELOPER_TESTS "Build human developer-owned test scaffold" ON)

include(CTest)

add_subdirectory(src/Engine)

if(ENGINE_BUILD_SANDBOX)
    add_subdirectory(src/Sandbox)
endif()

if(BUILD_TESTING AND ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
