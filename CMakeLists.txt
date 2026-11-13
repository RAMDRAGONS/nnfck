cmake_minimum_required(VERSION 3.20)
project(nnfck VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

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

# Only the decoder is needed; the assembly Huffman path is left out so the
# build stays plain C.
set(ZSTD_LIB ${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib)
if(NOT EXISTS ${ZSTD_LIB}/zstd.h)
  message(FATAL_ERROR "external/zstd is missing; run: git submodule update --init")
endif()
add_library(nnfck_zstd STATIC
  ${ZSTD_LIB}/common/debug.c
  ${ZSTD_LIB}/common/entropy_common.c
  ${ZSTD_LIB}/common/error_private.c
  ${ZSTD_LIB}/common/fse_decompress.c
  ${ZSTD_LIB}/common/xxhash.c
  ${ZSTD_LIB}/common/zstd_common.c
  ${ZSTD_LIB}/decompress/huf_decompress.c
  ${ZSTD_LIB}/decompress/zstd_ddict.c
  ${ZSTD_LIB}/decompress/zstd_decompress.c
  ${ZSTD_LIB}/decompress/zstd_decompress_block.c
)
target_compile_definitions(nnfck_zstd PRIVATE ZSTD_DISABLE_ASM)
target_include_directories(nnfck_zstd SYSTEM PUBLIC ${ZSTD_LIB})

add_library(nnfck_core STATIC
  src/core/data.cpp
  src/core/file.cpp
  src/core/json.cpp
  src/core/report.cpp
  src/core/shift_jis.cpp
  src/core/xml.cpp
  src/formats/registry.cpp
  src/formats/listing.cpp
  src/formats/byte_order.cpp
  src/formats/yaz/yaz.cpp
  src/formats/zstd/zstd.cpp
  src/formats/sarc/sarc.cpp
  src/formats/fres/fres.cpp
  src/formats/fres/common.cpp
  src/formats/fres/cafe.cpp
  src/formats/fres/nx.cpp
  src/formats/fres/fmaa.cpp
  src/formats/fres/fmdl.cpp
  src/formats/fres/fscn.cpp
  src/formats/fres/fsha.cpp
  src/formats/fres/fshu.cpp
  src/formats/fres/fska.cpp
  src/formats/fres/ftex.cpp
  src/formats/fres/ftxp.cpp
  src/formats/fres/fvis.cpp
  src/formats/fres/intermediate.cpp
  src/formats/bntx/bntx.cpp
  src/formats/gfx/gfx.cpp
  src/formats/gx2/gx2.cpp
  src/formats/byml/byml.cpp
  src/formats/eft/eftb.cpp
  src/formats/eft/eset.cpp
  src/formats/eft/spbd.cpp
  src/formats/layout/common.cpp
  src/formats/layout/flyt.cpp
  src/formats/layout/flan.cpp
  src/formats/layout/fcpx.cpp
  src/formats/layout/flim.cpp
  src/formats/xlink/xlink.cpp
  src/formats/xlink/elink.cpp
  src/formats/xlink/slink.cpp
  src/formats/xlink/pooled.cpp
  src/formats/pmaa/common.cpp
  src/formats/pmaa/pmaa.cpp
  src/formats/pmaa/text.cpp
  src/formats/sharc/sharc.cpp
  src/formats/bnsh/bnsh.cpp
  src/formats/fsha/fsha.cpp
  src/formats/xtx/xtx.cpp
  src/formats/nvn_shader/nvn_shader.cpp
  src/formats/pmaa/names.cpp
)
target_include_directories(nnfck_core PUBLIC src)
target_link_libraries(nnfck_core PRIVATE nnfck_zstd)

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

# Data files are looked up beside the executable, so builds can run in place,
# and under share/nnfck once installed.
set(NNFCK_DATA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/pmaa_names.json)
add_custom_target(nnfck_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NNFCK_DATA_FILES} $<TARGET_FILE_DIR:nnfck>
  VERBATIM)
install(TARGETS nnfck RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${NNFCK_DATA_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/nnfck)

option(NNFCK_BUILD_TESTS "Build the nnfck test suite" ON)
if(NNFCK_BUILD_TESTS)
  enable_testing()
  add_executable(nnfck_tests tests/tests.cpp)
  target_link_libraries(nnfck_tests PRIVATE nnfck_core)
  add_custom_target(nnfck_tests_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NNFCK_DATA_FILES} $<TARGET_FILE_DIR:nnfck_tests>
    VERBATIM)
  add_dependencies(nnfck_tests nnfck_tests_data)
  add_test(NAME nnfck_tests COMMAND nnfck_tests)
endif()
