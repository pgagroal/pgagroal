#
# Add files for libpgagroal
#
FILE(GLOB SOURCE_FILES "libpgagroal/*.c")
FILE(GLOB HEADER_FILES "include/*.h")

set(SOURCES ${SOURCE_FILES} ${HEADER_FILES})

function(pgagroal_require_shared_libs)
  foreach(lib_var IN LISTS ARGN)
    if (NOT DEFINED ${lib_var})
      continue()
    endif()

    set(updated_libs "")
    foreach(lib IN LISTS ${lib_var})
      if (IS_ABSOLUTE "${lib}" AND lib MATCHES "\\.a$")
        get_filename_component(lib_dir "${lib}" DIRECTORY)
        get_filename_component(lib_name "${lib}" NAME_WE)
        string(REGEX REPLACE "^lib" "" lib_base "${lib_name}")
        set(shared_lib "")
        if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
          file(GLOB shared_candidates
            "${lib_dir}/${lib_name}.dylib*"
            "${lib_dir}/${lib_base}.dylib*")
        else()
          file(GLOB shared_candidates
            "${lib_dir}/${lib_name}.so*"
            "${lib_dir}/${lib_base}.so*")
        endif()

        if (shared_candidates)
          list(GET shared_candidates 0 shared_lib)
        endif()

        if (shared_lib)
          list(APPEND updated_libs "${shared_lib}")
        else()
          message(FATAL_ERROR "Static library detected: ${lib}. Shared libraries are required.")
        endif()
      else()
        list(APPEND updated_libs "${lib}")
      endif()
    endforeach()
    set(${lib_var} "${updated_libs}" PARENT_SCOPE)
  endforeach()
endfunction()

#
# OS
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

  add_compile_options(-DHAVE_LINUX)
  add_compile_options(-D_POSIX_C_SOURCE=200809L)

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
    ${THREAD_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
  )

  #
  # Event library backend
  #
  if (NOT LIBURING_FOUND)
    message(FATAL_ERROR "liburing was not found")
  endif()

  include_directories(${LIBURING_INCLUDE_DIRS})

  pgagroal_require_shared_libs(
    OPENSSL_CRYPTO_LIBRARY
    OPENSSL_SSL_LIBRARY
    LIBATOMIC_LIBRARY
    THREAD_LIBRARY
    ZLIB_LIBRARIES
    BZIP2_LIBRARIES
    ZSTD_LIBRARIES
    LZ4_LIBRARIES
    LIBURING_LIBRARY
  )

  #
  # Library directories
  #
  link_libraries(
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${LIBATOMIC_LIBRARY}
    ${THREAD_LIBRARY}
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${LIBURING_LIBRARY}
  )

  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  find_program(HOMEBREW_EXECUTABLE brew)
  if(EXISTS ${HOMEBREW_EXECUTABLE})
    execute_process(
      COMMAND ${HOMEBREW_EXECUTABLE} --prefix
      OUTPUT_VARIABLE HOMEBREW_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Homebrew found at ${HOMEBREW_PREFIX}")
  endif()

  #
  # Detecting OpenSSL
  #
  execute_process(
    COMMAND ${HOMEBREW_EXECUTABLE} --prefix openssl
    OUTPUT_VARIABLE HOMEBREW_OPENSSL
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(DEFINED HOMEBREW_OPENSSL)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${HOMEBREW_OPENSSL}/include")
  else()
    message(FATAL_ERROR "Homebrew's OpenSSL needed")
  endif()

  add_compile_options(-D_DARWIN_C_SOURCE)
  add_compile_options(-DHAVE_OSX)

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIRS}
    ${THREAD_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
  )

  pgagroal_require_shared_libs(
    OPENSSL_LIBRARIES
    THREAD_LIBRARY
    ZLIB_LIBRARIES
    BZIP2_LIBRARIES
    ZSTD_LIBRARIES
    LZ4_LIBRARIES
  )

  #
  # Library directories
  #
  link_libraries(
    ${OPENSSL_LIBRARIES}
    ${THREAD_LIBRARY}
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LZ4_LIBRARIES}
  )
else()

  add_compile_options(-D_XOPEN_SOURCE=700)
  add_compile_options(-D_BSD_SOURCE)
  add_compile_options(-D_DEFAULT_SOURCE)
  add_compile_options(-D__BSD_VISIBLE)

  if (${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    add_compile_options(-DHAVE_OPENBSD)
  elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
     add_compile_options(-DHAVE_FREEBSD)
  endif()

  if (SYSTEMD_FOUND)
  add_compile_options(-DHAVE_SYSTEMD)

  include_directories(${SYSTEMD_INCLUDE_DIRS})
  link_libraries(${SYSTEMD_LIBRARIES})
  endif()

  #
  # Include directories
  #
  include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIRS}
    ${THREAD_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
  )

  pgagroal_require_shared_libs(
    SYSTEMD_LIBRARIES
    OPENSSL_LIBRARIES
    THREAD_LIBRARY
    ZLIB_LIBRARIES
    BZIP2_LIBRARIES
    ZSTD_LIBRARIES
    LZ4_LIBRARIES
  )

  #
  # Library directories
  #
  link_libraries(
    ${OPENSSL_LIBRARIES}
    ${THREAD_LIBRARY}
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${LZ4_LIBRARIES}
  )
endif()

#
# Compile options
#
add_compile_options(-g)
add_compile_options(-Wall)
add_compile_options(-Werror)
add_compile_options(-std=c17)
add_compile_options(-D__USE_ISOC11)
add_compile_options(-D_GNU_SOURCE)
add_compile_options(-Wno-deprecated)
add_compile_options(-Wno-deprecated-declarations)

#
# version number and string management
#
add_compile_options(-DPGAGROAL_MAJOR_VERSION=${VERSION_MAJOR})
add_compile_options(-DPGAGROAL_MINOR_VERSION=${VERSION_MINOR})
add_compile_options(-DPGAGROAL_PATCH_VERSION=${VERSION_PATCH})
add_compile_options(-DPGAGROAL_VERSION="${VERSION_STRING}")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wstrict-prototypes)
endif()

check_c_compiler_flag(-fPIC HAS_PIC)
if (HAS_PIC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

check_include_file(execinfo.h HAVE_EXECINFO_H)
if (HAVE_EXECINFO_H)
  add_compile_options(-DHAVE_EXECINFO_H)
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  include(CheckPIESupported)
  check_pie_supported()
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_compile_options(-O0)
  add_compile_options(-DDEBUG)

  check_c_compiler_flag(-Wformat HAS_FORMAT)
  if (HAS_FORMAT)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat")
  endif()

  check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
  if (HAS_FORMAT_SECURITY)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat-security")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACK_PROTECTOR_STRONG)
  if (HAS_STACK_PROTECTOR_STRONG)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fstack-protector-strong")
  endif()

  check_c_compiler_flag(-rdynamic HAS_DYNAMIC)
  if (HAS_DYNAMIC)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACKPROTECTOR_STRONG)
  if (HAS_STACKPROTECTOR_STRONG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
  else()
    check_c_compiler_flag(-fstack-protector HAS_STACKPROTECTOR)
    if (HAS_STACKPROTECTOR)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
    endif()
  endif()

  check_c_compiler_flag(-Wl,arg HAS_LINKER)
  check_linker_flag(C "-z relro" LINKER_HAS_RELRO)
  if (HAS_LINKER AND LINKER_HAS_RELRO)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
  endif()

  check_linker_flag(C "-z now" LINKER_HAS_NOW)
  if (HAS_LINKER AND LINKER_HAS_NOW)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,now")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
  endif()

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fprofile-instr-generate)
    add_compile_options(-fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
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
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
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

  check_c_compiler_flag(-fno-omit-frame-pointer HAS_NO_OMIT_FRAME_POINTER)
  if (HAS_NO_OMIT_FRAME_POINTER)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer")
  endif()
  
  check_c_compiler_flag(-Wextra HAS_WEXTRA)
  if (HAS_WEXTRA)
    if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin" AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
      add_compile_options(-Wextra)
      
      check_c_compiler_flag(-Wno-sign-compare HAS_NO_SIGN_COMPARE)
      if (HAS_NO_SIGN_COMPARE)
        add_compile_options(-Wno-sign-compare)
      endif()
    endif()
  endif()

  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -g")
endif()

if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)

  check_c_compiler_flag(-Wformat HAS_FORMAT)
  if (HAS_FORMAT)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat")
  endif()

  check_c_compiler_flag(-Wformat-security HAS_FORMAT_SECURITY)
  if (HAS_FORMAT_SECURITY)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wformat-security")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACK_PROTECTOR_STRONG)
  if (HAS_STACK_PROTECTOR_STRONG)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fstack-protector-strong")
  endif()

  check_c_compiler_flag(-rdynamic HAS_DYNAMIC)
  if (HAS_DYNAMIC)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
  endif()

  check_c_compiler_flag(-fstack-protector-strong HAS_STACKPROTECTOR_STRONG)
  if (HAS_STACKPROTECTOR_STRONG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
  else()
    check_c_compiler_flag(-fstack-protector HAS_STACKPROTECTOR)
    if (HAS_STACKPROTECTOR)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector")
    endif()
  endif()

  check_c_compiler_flag(-Wl,arg HAS_LINKER)
  check_linker_flag(C "-z relro" LINKER_HAS_RELRO)
  if (HAS_LINKER AND HAS_RELRO)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
  endif()

  check_linker_flag(C "-z now" LINKER_HAS_NOW)
  if (HAS_LINKER AND LINKER_HAS_NOW)
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,now")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
  endif()
endif (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)

if (CMAKE_BUILD_TYPE MATCHES Performance)
  add_compile_options(-O2)
  add_compile_options(-DNDEBUG)

  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-march=native)
    add_compile_options(-mtune=native)
    add_compile_options(-flto)
  endif()
endif (CMAKE_BUILD_TYPE MATCHES Performance)

#
# Build libpgagroal
#
add_library(pgagroal SHARED ${SOURCES})
set_target_properties(pgagroal PROPERTIES LINKER_LANGUAGE C VERSION ${VERSION_STRING}
                               SOVERSION ${VERSION_MAJOR})
target_link_libraries(pgagroal PUBLIC)

install(TARGETS pgagroal DESTINATION ${CMAKE_INSTALL_LIBDIR}/)

#
# Build pgagroal
#
add_executable(pgagroal-bin main.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal)
else()
  set_target_properties(pgagroal-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal)
endif()
target_link_libraries(pgagroal-bin pgagroal)

install(TARGETS pgagroal-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-cli
#
add_executable(pgagroal-cli-bin cli.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-cli-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal-cli)
else()
  set_target_properties(pgagroal-cli-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal-cli)
endif()
target_link_libraries(pgagroal-cli-bin pgagroal)

install(TARGETS pgagroal-cli-bin DESTINATION ${CMAKE_INSTALL_BINDIR})

#
# Build pgagroal-admin
#
add_executable(pgagroal-admin-bin admin.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-admin-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal-admin)
else()
  set_target_properties(pgagroal-admin-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal-admin)
endif()
target_link_libraries(pgagroal-admin-bin pgagroal)

install(TARGETS pgagroal-admin-bin DESTINATION ${CMAKE_INSTALL_BINDIR})



#
# Build pgagroal-vault
#
add_executable(pgagroal-vault-bin vault.c ${RESOURCE_OBJECT})
if (CMAKE_C_LINK_PIE_SUPPORTED)
  set_target_properties(pgagroal-vault-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE TRUE OUTPUT_NAME pgagroal-vault)
else()
  set_target_properties(pgagroal-vault-bin PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE FALSE OUTPUT_NAME pgagroal-vault)
endif()
target_link_libraries(pgagroal-vault-bin pgagroal)

install(TARGETS pgagroal-vault-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
