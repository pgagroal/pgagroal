if (check)

  file(GLOB_RECURSE TEST_SOURCES
    "libpgagroaltest/*.c"
    "testcases/*.c")
  file(GLOB_RECURSE TEST_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

  set(SOURCES ${TEST_SOURCES} ${TEST_HEADERS})

  if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_options(-O0)
    add_compile_options(-DDEBUG)
  endif()

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
      if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.2)
          add_compile_options(-fsanitize=address)
          add_compile_options(-fno-sanitize=null)
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.3)
            add_compile_options(-fsanitize=undefined)
            add_compile_options(-fsanitize=float-divide-by-zero)
            add_compile_options(-fsanitize=float-cast-overflow)
          endif()
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.6)
            add_compile_options(-fno-sanitize-recover=all)
          endif()
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.8)
            add_compile_options(-fsanitize-recover=address)
          endif()
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.9)
            add_compile_options(-fsanitize-address-use-after-scope)
          endif()
          
          add_compile_options(-fno-sanitize=alignment)

          set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment")
          set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment")        
        endif()
      endif()
    endif()
  elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
      if (NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 4.8)
          add_compile_options(-fsanitize=address)
          
          set(TEMP_EXE_LINKER_FLAGS "-fsanitize=address")
          set(TEMP_SHARED_LINKER_FLAGS "-fsanitize=address")
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 4.9)
            add_compile_options(-fsanitize=undefined)
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fsanitize=undefined")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fsanitize=undefined")
          endif()
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 5.1)
            add_compile_options(-fno-sanitize=alignment)
            add_compile_options(-fno-sanitize-recover=all)
            add_compile_options(-fsanitize=float-cast-overflow)
            add_compile_options(-fsanitize=float-divide-by-zero)
            
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fno-sanitize=alignment -fno-sanitize-recover=all -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fno-sanitize=alignment -fno-sanitize-recover=all -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero")
          endif()
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 6.0)
            add_compile_options(-fsanitize-recover=address)
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fsanitize-recover=address")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fsanitize-recover=address")
          endif()
          
          if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 7.0)
            add_compile_options(-fsanitize-address-use-after-scope)
            set(TEMP_EXE_LINKER_FLAGS "${TEMP_EXE_LINKER_FLAGS} -fsanitize-address-use-after-scope")
            set(TEMP_SHARED_LINKER_FLAGS "${TEMP_SHARED_LINKER_FLAGS} -fsanitize-address-use-after-scope")
          endif()
          
          set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TEMP_EXE_LINKER_FLAGS}")
          set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${TEMP_SHARED_LINKER_FLAGS}")
        endif()
      endif()
    endif()
  endif()

  add_executable(pgagroal_test runner.c ${SOURCES})
    if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
  target_compile_options(pgexporter-test PRIVATE
    -fprofile-instr-generate
    -fcoverage-mapping
    -O0
  )
  target_link_options(pgexporter-test PRIVATE
    -fprofile-instr-generate
    -fcoverage-mapping
  )
endif()

  target_include_directories(pgagroal_test PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_SOURCE_DIR}/test/include)

  if(EXISTS "/etc/debian_version")
    target_link_libraries(pgagroal_test Check::check subunit pthread rt m pgagroal)
  elseif(APPLE)
    target_link_libraries(pgagroal_test Check::check m pgagroal)
  else()
    target_link_libraries(pgagroal_test Check::check pthread rt m pgagroal)
  endif()

  add_custom_target(custom_clean
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o pgagroal_test
    COMMENT "Cleaning up..."
  )
endif()

if(container)

add_test(NAME container_test
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check.sh
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set_property(TEST container_test
             PROPERTY FAIL_REGULAR_EXPRESSION "Failures: [1-9][0-9]*")

endif()

file(
    COPY
    "${CMAKE_SOURCE_DIR}/test/conf"
    DESTINATION "${CMAKE_BINARY_DIR}/"
)
configure_file(
  "${CMAKE_SOURCE_DIR}/test/check.sh"
  "${CMAKE_BINARY_DIR}/check.sh"
  COPYONLY
)
