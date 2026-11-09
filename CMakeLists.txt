cmake_minimum_required(VERSION 3.20)
project(nnfck VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(WARNING "nnfck is built with LLVM clang; configure with a preset or -DCMAKE_TOOLCHAIN_FILE=cmake/llvm.cmake")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(nnfck_core STATIC
  src/core/file.cpp
  src/core/report.cpp
  src/formats/registry.cpp
  src/formats/yaz/yaz.cpp
  src/formats/sarc/sarc.cpp
  src/formats/bfres/bfres.cpp
  src/formats/byml/byml.cpp
  src/formats/havok/packfile.cpp
  src/formats/eft/eftb.cpp
)
target_include_directories(nnfck_core PUBLIC src)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(nnfck_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
elseif(MSVC)
  target_compile_options(nnfck_core PUBLIC /W4 /permissive-)
endif()

option(NNFCK_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(NNFCK_SANITIZE)
  target_compile_options(nnfck_core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(nnfck_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(nnfck src/main.cpp)
target_link_libraries(nnfck PRIVATE nnfck_core)
target_compile_definitions(nnfck PRIVATE NNFCK_VERSION="${PROJECT_VERSION}")

option(NNFCK_BUILD_TESTS "Build the nnfck test suite" ON)
if(NNFCK_BUILD_TESTS)
  enable_testing()
  add_executable(nnfck_tests tests/tests.cpp)
  target_link_libraries(nnfck_tests PRIVATE nnfck_core)
  add_test(NAME nnfck_tests COMMAND nnfck_tests)
endif()
