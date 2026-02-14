#
# pthread support
#

find_path(THREAD_INCLUDE_DIR
  NAMES
    pthread.h
)
find_library(THREAD_LIBRARY
  NAMES
    pthread pthread.so.0 libpthread.so.0
  HINTS
    /usr/local/lib64
    /usr/local/lib
    /opt/local/lib64
    /opt/local/lib
    /usr/lib64
    /usr/lib
    /lib64
    /lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(THREAD DEFAULT_MSG
                                  THREAD_LIBRARY THREAD_INCLUDE_DIR)

if(THREAD_FOUND)
  set(THREAD_LIBRARIES ${THREAD_LIBRARY})
  set(THREAD_INCLUDE_DIRS ${THREAD_INCLUDE_DIR})
endif()

mark_as_advanced(THREAD_INCLUDE_DIR THREAD_LIBRARY)
