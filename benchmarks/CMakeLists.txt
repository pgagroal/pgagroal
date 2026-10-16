#
# pgagroal-bench: the C part of benchmarks/bench.sh
#
if (NOT CMAKE_BUILD_TYPE MATCHES Release)
  message(FATAL_ERROR "The benchmark harness needs -DCMAKE_BUILD_TYPE=Release")
endif()

FILE(GLOB BENCH_SOURCES "src/*.c")

add_executable(pgagroal-bench ${BENCH_SOURCES})

target_include_directories(pgagroal-bench PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/include
  ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(pgagroal-bench pgagroal)
