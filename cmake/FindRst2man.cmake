# RST2MAN_FOUND - true if the program was found
# RST2MAN_VERSION - version of rst2man
# RST2MAN_EXECUTABLE - path to the rst2man program

find_program(RST2MAN_EXECUTABLE
 NAMES rst2man rst2man-3.11 rst2man-3.10 rst2man-3.9 rst2man-3.py
 DOC "The Python Docutils generator of Unix Manpages from reStructuredText"
)










if (RST2MAN_EXECUTABLE)
  #Get the version string
  execute_process(
    COMMAND ${RST2MAN_EXECUTABLE} --version
    OUTPUT_VARIABLE rst2man_version_str
  )
  # Expected format: rst2man (Docutils 0.13.1 [release], Python 2.7.15, on linux2)
  string(REGEX REPLACE "^rst2man[\t ]+\\(Docutils[\t ]+([^\t ]*).*" "\\1"
         RST2MAN_VERSION "${rst2man_version_str}")
  unset(rst2man_version_str)
endif()

# handle the QUIETLY and REQUIRED arguments and set RST2MAN_FOUND to TRUE
# if all listed variables are set
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Rst2man
  REQUIRED_VARS RST2MAN_EXECUTABLE
  VERSION_VAR RST2MAN_VERSION
)

mark_as_advanced(RST2MAN_EXECUTABLE RST2MAN_VERSION)
