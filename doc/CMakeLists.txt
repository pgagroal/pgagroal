if (RST2MAN_FOUND)
  # man target
  add_custom_target(man ALL)

  # man page definitions
  set(PGAGROAL_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal.1.rst")
  set(PGAGROAL_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal.1")
  set(PGAGROAL_CLI_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal-cli.1.rst")
  set(PGAGROAL_CLI_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal-cli.1")
  set(PGAGROAL_ADMIN_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal-admin.1.rst")
  set(PGAGROAL_ADMIN_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal-admin.1")
  set(PGAGROAL_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal.conf.5.rst")
  set(PGAGROAL_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal.conf.5")
  set(PGAGROAL_HBA_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal_hba.conf.5.rst")
  set(PGAGROAL_HBA_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal_hba.conf.5")
  set(PGAGROAL_DATABASES_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal_databases.conf.5.rst")
  set(PGAGROAL_DATABASES_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal_databases.conf.5")
  set(PGAGROAL_VAULT_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal-vault.1.rst")
  set(PGAGROAL_VAULT_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal-vault.1")
  set(PGAGROAL_VAULT_CONF_SRC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/man/pgagroal_vault.conf.5.rst")
  set(PGAGROAL_VAULT_CONF_DST_FILE "${CMAKE_CURRENT_BINARY_DIR}/pgagroal_vault.conf.5")

  # pgagroal.1
  add_custom_command(
    OUTPUT ${PGAGROAL_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_SRC_FILE} ${PGAGROAL_DST_FILE}
    DEPENDS ${PGAGROAL_SRC_FILE}
    COMMENT "Generating man page: pgagroal.1"
  )

  # pgagroal-cli.1
  add_custom_command(
    OUTPUT ${PGAGROAL_CLI_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_CLI_SRC_FILE} ${PGAGROAL_CLI_DST_FILE}
    DEPENDS ${PGAGROAL_CLI_SRC_FILE}
    COMMENT "Generating man page: pgagroal-cli.1"
  )

  # pgagroal-admin.1
  add_custom_command(
    OUTPUT ${PGAGROAL_ADMIN_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_ADMIN_SRC_FILE} ${PGAGROAL_ADMIN_DST_FILE}
    DEPENDS ${PGAGROAL_ADMIN_SRC_FILE}
    COMMENT "Generating man page: pgagroal-admin.1"
  )

  # pgagroal.conf.5
  add_custom_command(
    OUTPUT ${PGAGROAL_CONF_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_CONF_SRC_FILE} ${PGAGROAL_CONF_DST_FILE}
    DEPENDS ${PGAGROAL_CONF_SRC_FILE}
    COMMENT "Generating man page: pgagroal.conf.5"
  )

  # pgagroal_hba.conf.5
  add_custom_command(
    OUTPUT ${PGAGROAL_HBA_CONF_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_HBA_CONF_SRC_FILE} ${PGAGROAL_HBA_CONF_DST_FILE}
    DEPENDS ${PGAGROAL_HBA_CONF_SRC_FILE}
    COMMENT "Generating man page: pgagroal_hba.conf.5"
  )

  # pgagroal_databases.conf.5
  add_custom_command(
    OUTPUT ${PGAGROAL_DATABASES_CONF_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_DATABASES_CONF_SRC_FILE} ${PGAGROAL_DATABASES_CONF_DST_FILE}
    DEPENDS ${PGAGROAL_DATABASES_CONF_SRC_FILE}
    COMMENT "Generating man page: pgagroal_databases.conf.5"
  )

  # pgagroal-vault.1
  add_custom_command(
    OUTPUT ${PGAGROAL_VAULT_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_VAULT_SRC_FILE} ${PGAGROAL_VAULT_DST_FILE}
    DEPENDS ${PGAGROAL_VAULT_SRC_FILE}
    COMMENT "Generating man page: pgagroal-vault.1"
  )

  # pgagroal_vault.conf.5
  add_custom_command(
    OUTPUT ${PGAGROAL_VAULT_CONF_DST_FILE}
    COMMAND ${RST2MAN_EXECUTABLE} ${PGAGROAL_VAULT_CONF_SRC_FILE} ${PGAGROAL_VAULT_CONF_DST_FILE}
    DEPENDS ${PGAGROAL_VAULT_CONF_SRC_FILE}
    COMMENT "Generating man page: pgagroal_vault.conf.5"
  )

  # Group man page outputs into a target
  add_custom_target(manpages ALL
    DEPENDS
    ${PGAGROAL_DST_FILE}
    ${PGAGROAL_CLI_DST_FILE}
    ${PGAGROAL_ADMIN_DST_FILE}
    ${PGAGROAL_VAULT_DST_FILE}
    ${PGAGROAL_CONF_DST_FILE}
    ${PGAGROAL_HBA_CONF_DST_FILE}
    ${PGAGROAL_DATABASES_CONF_DST_FILE}
    ${PGAGROAL_VAULT_CONF_DST_FILE}
  )

  add_dependencies(man manpages)

  #
  # Install configuration and documentation
  #
  install(FILES etc/pgagroal.conf DESTINATION share/doc/pgagroal/etc)
  install(FILES etc/pgagroal_hba.conf DESTINATION share/doc/pgagroal/etc)

  install(DIRECTORY . DESTINATION share/doc/pgagroal FILES_MATCHING PATTERN "*.md" PATTERN "etc" EXCLUDE PATTERN "images" EXCLUDE PATTERN "man" EXCLUDE PATTERN "manual" EXCLUDE)
  install(DIRECTORY images/ DESTINATION share/doc/pgagroal/images FILES_MATCHING PATTERN "*.png")

  install(FILES ${PGAGROAL_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGAGROAL_CLI_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGAGROAL_ADMIN_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGAGROAL_VAULT_DST_FILE} DESTINATION share/man/man1)
  install(FILES ${PGAGROAL_CONF_DST_FILE} DESTINATION share/man/man5)
  install(FILES ${PGAGROAL_HBA_CONF_DST_FILE} DESTINATION share/man/man5)
  install(FILES ${PGAGROAL_DATABASES_CONF_DST_FILE} DESTINATION share/man/man5)
  install(FILES ${PGAGROAL_VAULT_CONF_DST_FILE} DESTINATION share/man/man5)
endif()

#
# Generate manual
#
if (DOCS)
  FILE(GLOB EN "manual/en/??-*.md")

  # Manuals definitions
  set(MANUAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/doc")
  get_filename_component(PARENT_DIR ${CMAKE_BINARY_DIR} DIRECTORY)
  set(IMAGE_DIR "${PARENT_DIR}/doc/images")

  add_custom_target(
    copy_images
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${IMAGE_DIR} ${CMAKE_BINARY_DIR}/images
    COMMENT "Copy images"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-en.pdf
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-en.pdf --resource-path=${CMAKE_BINARY_DIR} --from markdown --template eisvogel --listings -N --toc ${EN}
    DEPENDS ${EN}
    COMMENT "Generating English PDF documentation"
  )

  add_custom_command(
    OUTPUT ${MANUAL_OUTPUT_DIR}/pgagroal-en.html
    COMMAND ${PANDOC_EXECUTABLE} -o ${MANUAL_OUTPUT_DIR}/pgagroal-en.html --resource-path=${CMAKE_BINARY_DIR} -s -f markdown-smart -N --toc -t html5 ${EN}
    DEPENDS ${EN}
    COMMENT "Generating English HTML documentation"
  )

  add_custom_target(
    all_docs ALL
    DEPENDS copy_images ${MANUAL_OUTPUT_DIR}/pgagroal-en.pdf ${MANUAL_OUTPUT_DIR}/pgagroal-en.html
  )

  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-en.pdf DESTINATION share/doc/pgagroal/manual)
  install(FILES ${MANUAL_OUTPUT_DIR}/pgagroal-en.html DESTINATION share/doc/pgagroal/manual)

  #
  # Generate API docs
  #
  if (DOXYGEN_FOUND)
    add_custom_target(api ALL)

    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_MAN NO)
    set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.doxygen)

    doxygen_add_docs(doxygen ${PROJECT_SOURCE_DIR}/src/include/)

    if (${DOXYGEN_VERSION} STREQUAL "1.10.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.11.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.12.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.13.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.13.1" OR
        ${DOXYGEN_VERSION} STREQUAL "1.13.2" OR
        ${DOXYGEN_VERSION} STREQUAL "1.14.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.15.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.16.0" OR
        ${DOXYGEN_VERSION} STREQUAL "1.16.1")

      add_custom_command(
        TARGET api
        COMMAND ${DOXYGEN_EXECUTABLE} -q ${DOXYFILE_OUT}
        COMMENT "Generating API documentation"
        POST_BUILD
      )
    else()
      add_custom_command(
        TARGET api
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        COMMENT "Generating API documentation"
        POST_BUILD
      )
    endif()
  endif()
endif()
