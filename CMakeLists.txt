cmake_minimum_required(VERSION 3.22)

project(OpenHDK VERSION 0.1.0 LANGUAGES CXX)

option(OPENHDK_BUILD_TESTS "Build OpenHDK tests" ON)

add_library(openhdk_core INTERFACE)
add_library(OpenHDK::core ALIAS openhdk_core)
target_include_directories(openhdk_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(openhdk_core INTERFACE cxx_std_20)

add_executable(OpenHDK app/main.cpp)
target_link_libraries(OpenHDK PRIVATE OpenHDK::core)

if(OPENHDK_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(openhdk_bootstrap_smoke tests/bootstrap_smoke.cpp)
        target_link_libraries(openhdk_bootstrap_smoke PRIVATE OpenHDK::core)
        add_test(NAME bootstrap-smoke COMMAND openhdk_bootstrap_smoke)
    endif()
endif()
