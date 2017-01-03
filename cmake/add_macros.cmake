macro (_default_if_unset VAR VAL)
  if (NOT ${VAR})
    set (${VAR} ${VAL})
  endif()
endmacro()

macro (_bundle_target TARGET_NAME INSTALL_DESTINATION RPATH_VARIABLE CREATE_INFO)
  find_program (CHRPATH_BINARY NAMES chrpath DOC "chrpath is required for bundling")
  if (NOT CHRPATH_BINARY)
    message (FATAL_ERROR "Unable to find chrpath (CHRPATH_BINARY), which is required for "
                         "bundling executables and libraries correctly")
  endif()

  add_custom_command (OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bundle-${TARGET_NAME}"
    COMMAND "${CMAKE_SOURCE_DIR}/cmake/bundle.sh"
    ARGS "${CMAKE_CURRENT_BINARY_DIR}/bundle-${TARGET_NAME}"
         "${CHRPATH_BINARY}"
         $<TARGET_FILE:${TARGET_NAME}>
    DEPENDS $<TARGET_FILE:${TARGET_NAME}>
            "${CMAKE_SOURCE_DIR}/cmake/bundle.sh"
  )
  add_custom_target (${TARGET_NAME}-bundled-libraries ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bundle-${TARGET_NAME}"
  )

  install (DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bundle-${TARGET_NAME}/"
    DESTINATION "libexec/bundle/lib"
    USE_SOURCE_PERMISSIONS
  )

  if (${CREATE_INFO})
    add_custom_command (OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bundle-info/${TARGET_NAME}"
      COMMAND "mkdir" "-p" "${CMAKE_CURRENT_BINARY_DIR}/bundle-info/"
      COMMAND "find" "." "-type" "f"
              "|"
              "sed" "-e" "s,^\.,libexec/bundle/lib,"
              ">" "${CMAKE_CURRENT_BINARY_DIR}/bundle-info/${TARGET_NAME}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bundle-${TARGET_NAME}"
      DEPENDS ${TARGET_NAME}-bundled-libraries
    )
    add_custom_target (${TARGET_NAME}-bundled-libraries-info ALL
      DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bundle-info/${TARGET_NAME}"
    )

    install (FILES "${CMAKE_CURRENT_BINARY_DIR}/bundle-info/${TARGET_NAME}"
      DESTINATION "libexec/bundle/info"
    )
  endif()

  string (REGEX REPLACE "[^/]+" ".." RPATH_MID "${INSTALL_DESTINATION}")
  list (APPEND ${RPATH_VARIABLE} "\$ORIGIN/${RPATH_MID}/libexec/bundle/lib")
endmacro()

macro (_rpath TARGET_NAME RPATH)
  set_property (TARGET ${TARGET_NAME} APPEND
    PROPERTY LINK_FLAGS "-Wl,--disable-new-dtags"
  )

  set_property (TARGET ${TARGET_NAME} PROPERTY SKIP_RPATH false)

  #! \note need proper rpath in build output for bundling
  set_property (TARGET ${TARGET_NAME} PROPERTY SKIP_BUILD_RPATH false)
  set_property (TARGET ${TARGET_NAME} PROPERTY BUILD_WITH_INSTALL_RPATH false)

  set_property (TARGET ${TARGET_NAME} PROPERTY INSTALL_RPATH ${RPATH})
  set_property (TARGET ${TARGET_NAME} PROPERTY INSTALL_RPATH_USE_LINK_PATH false)
endmacro()

macro (_moc TARGET_VAR)
  set (HACK_OPTIONS)
  list (APPEND HACK_OPTIONS "-DBOOST_LEXICAL_CAST_INCLUDED")
  list (APPEND HACK_OPTIONS "-DBOOST_TT_HAS_OPERATOR_HPP_INCLUDED")
  list (APPEND HACK_OPTIONS "-DBOOST_NO_TEMPLATE_PARTIAL_SPECIALIZATION")
  #! \note next_prior.hpp in 1.57.0 includes those directly
  list (APPEND HACK_OPTIONS "-DBOOST_TT_HAS_PLUS_HPP_INCLUDED")
  list (APPEND HACK_OPTIONS "-DBOOST_TT_HAS_PLUS_ASSIGN_HPP_INCLUDED")
  list (APPEND HACK_OPTIONS "-DBOOST_TT_HAS_MINUS_HPP_INCLUDED")
  list (APPEND HACK_OPTIONS "-DBOOST_TT_HAS_MINUS_ASSIGN_HPP_INCLUDED")

  if (TARGET Qt4::QtCore)
    qt4_wrap_cpp (${TARGET_VAR} ${ARGN} OPTIONS "${HACK_OPTIONS}")
  endif()
endmacro()

macro (extended_add_library)
  if (TARGET Qt4::QtCore)
    set (QT_OPTIONS MOC)
  endif()

  set (options POSITION_INDEPENDENT PRECOMPILED INSTALL CREATE_BUNDLE_INFO)
  set (one_value_options NAME NAMESPACE TYPE INSTALL_DESTINATION)
  set (multi_value_options
    LIBRARIES ${QT_OPTIONS} SOURCES PUBLIC_HEADERS INCLUDE_DIRECTORIES RPATH
    SYSTEM_INCLUDE_DIRECTORIES
  )
  set (required_options NAME)
  parse_arguments (ARG "${options}" "${one_value_options}" "${multi_value_options}" "${required_options}" ${ARGN})

  _default_if_unset (ARG_TYPE "STATIC")
  _default_if_unset (ARG_INSTALL_DESTINATION "lib")

  if (ARG_NAMESPACE)
    set (target_name "${ARG_NAMESPACE}-${ARG_NAME}")
  else()
    set (target_name "${ARG_NAME}")
  endif()

  if (NOT (${ARG_TYPE} STREQUAL "STATIC" OR ${ARG_TYPE} STREQUAL "SHARED" OR ${ARG_TYPE} STREQUAL "MODULE"))
    message (FATAL_ERROR "Bad library type: ${ARG_TYPE}")
  endif()

  if ((NOT ARG_SOURCES AND NOT ARG_MOC) OR ARG_PRECOMPILED)
    add_library (${target_name} INTERFACE)

    if (ARG_PRECOMPILED)
      if (ARG_TYPE STREQUAL "STATIC")
        list (APPEND ARG_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/lib${target_name}.a")
      else()
        list (APPEND ARG_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/lib${target_name}.so")
      endif()
    endif()

    target_link_libraries (${target_name} INTERFACE ${ARG_LIBRARIES})
  else()
    _moc (${ARG_NAME}_mocced ${ARG_MOC})

    add_library (${target_name} ${ARG_TYPE} ${${ARG_NAME}_mocced} ${ARG_SOURCES})

    target_link_libraries (${target_name} ${ARG_LIBRARIES})
  endif()
  if (ARG_NAMESPACE)
    add_library (${ARG_NAMESPACE}::${ARG_NAME} ALIAS ${target_name})
  endif()
  if (ARG_PUBLIC_HEADERS)
    set_property (TARGET ${target_name} APPEND
      PROPERTY PUBLIC_HEADER ${ARG_PUBLIC_HEADERS}
    )
  endif()
  set (maybe_INTERFACE_)
  if (NOT ARG_SOURCES AND NOT ARG_MOC)
    set (maybe_INTERFACE_ INTERFACE_)
  endif()
  if (ARG_SYSTEM_INCLUDE_DIRECTORIES)
    set_property (TARGET ${target_name} APPEND
      PROPERTY ${maybe_INTERFACE_}SYSTEM_INCLUDE_DIRECTORIES ${ARG_SYSTEM_INCLUDE_DIRECTORIES}
    )
    list (APPEND ARG_INCLUDE_DIRECTORIES ${ARG_SYSTEM_INCLUDE_DIRECTORIES})
  endif()
  if (ARG_INCLUDE_DIRECTORIES)
    set_property (TARGET ${target_name} APPEND
      PROPERTY ${maybe_INTERFACE_}INCLUDE_DIRECTORIES ${ARG_INCLUDE_DIRECTORIES}
    )
  endif()
  if (ARG_POSITION_INDEPENDENT)
    set_property (TARGET ${target_name} APPEND
      PROPERTY COMPILE_FLAGS -fPIC
    )
  endif()

  if (ARG_INSTALL)
    install (TARGETS ${target_name}
      LIBRARY DESTINATION "${ARG_INSTALL_DESTINATION}"
      ARCHIVE DESTINATION "${ARG_INSTALL_DESTINATION}"
    )

    _bundle_target ("${target_name}" "${ARG_INSTALL_DESTINATION}" ARG_RPATH ${ARG_CREATE_BUNDLE_INFO})
    if (NOT ${ARG_TYPE} STREQUAL "STATIC")
      _rpath ("${target_name}" ${ARG_RPATH})
    endif()
  endif()
endmacro()

macro (extended_add_executable)
  if (TARGET Qt4::QtCore)
    set (QT_OPTIONS MOC)
  endif()

  set (options INSTALL DONT_APPEND_EXE_SUFFIX CREATE_BUNDLE_INFO)
  set (one_value_options NAME INSTALL_DESTINATION)
  set (multi_value_options LIBRARIES ${QT_OPTIONS} SOURCES RPATH)
  set (required_options NAME SOURCES)
  parse_arguments (ARG "${options}" "${one_value_options}" "${multi_value_options}" "${required_options}" ${ARGN})

  _default_if_unset (ARG_INSTALL_DESTINATION "bin")

  if (NOT ARG_DONT_APPEND_EXE_SUFFIX)
    set (target_name ${ARG_NAME}.exe)
  else()
    set (target_name ${ARG_NAME})
  endif()

  _moc (${ARG_NAME}_mocced ${ARG_MOC})

  add_executable (${target_name} ${${ARG_NAME}_mocced} ${ARG_SOURCES})
  target_link_libraries (${target_name} ${ARG_LIBRARIES})

  if (ARG_INSTALL)
    install (TARGETS ${target_name} RUNTIME DESTINATION "${ARG_INSTALL_DESTINATION}")

    string (REGEX REPLACE "[^/]+" ".." PATH_MID "${ARG_INSTALL_DESTINATION}")
    target_compile_definitions (${target_name} PRIVATE "-DINSTALLATION_HOME=\"${PATH_MID}\"")

    _bundle_target ("${target_name}" "${ARG_INSTALL_DESTINATION}" ARG_RPATH ${ARG_CREATE_BUNDLE_INFO})
    _rpath ("${target_name}" ${ARG_RPATH})
  endif()
endmacro()

macro (add_imported_executable)
  set (options)
  set (one_value_options NAME NAMESPACE LOCATION)
  set (multi_value_options)
  set (required_options NAME LOCATION)
  parse_arguments (ARG "${options}" "${one_value_options}" "${multi_value_options}" "${required_options}" ${ARGN})

  if (ARG_NAMESPACE)
    set (target_name ${ARG_NAMESPACE}::${ARG_NAME})
  else()
    set (target_name ${ARG_NAME})
  endif()

  add_executable (${target_name} IMPORTED)
  set_property (TARGET ${target_name} PROPERTY IMPORTED_LOCATION ${ARG_LOCATION})
endmacro()

macro (add_unit_test)
  if (TARGET Qt4::QtCore)
    set (QT_OPTIONS MOC)
  endif()

  set (options USE_BOOST PERFORMANCE_TEST)
  set (one_value_options NAME DESCRIPTION)
  set (multi_value_options LIBRARIES ${QT_OPTIONS} SOURCES INCLUDE ARGS DEPENDS)
  set (required_options NAME SOURCES)
  parse_arguments (ARG "${options}" "${one_value_options}" "${multi_value_options}" "${required_options}" ${ARGN})

  _default_if_unset (ARG_DESCRIPTION "${ARG_NAME}")

  set (target_name ${ARG_NAME}.test)

  _moc (${ARG_NAME}_mocced ${ARG_MOC})

  add_executable (${target_name} ${${ARG_NAME}_mocced} ${ARG_SOURCES})

  if (ARG_USE_BOOST)
    list (APPEND ARG_LIBRARIES Boost::test_exec_monitor)
    list (APPEND ARG_LIBRARIES Boost::unit_test_framework)
    target_compile_definitions (${target_name} PRIVATE
      "-DBOOST_TEST_MODULE=${ARG_DESCRIPTION}"
    )
  endif()

  target_include_directories (${target_name} PRIVATE ${ARG_INCLUDE})

  target_link_libraries (${target_name} ${ARG_LIBRARIES})
  add_test (NAME ${ARG_NAME} COMMAND $<TARGET_FILE:${target_name}> ${ARG_ARGS})

  if (ARG_DEPENDS)
    add_dependencies (${target_name} ${ARG_DEPENDS})
  endif()

  if (ARG_PERFORMANCE_TEST)
    set_property (TEST ${ARG_NAME} APPEND PROPERTY LABELS "performance_test")
  endif()
endmacro()
