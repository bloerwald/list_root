macro (find_boost)
  set (options FROM_GPISPACE_INSTALLATION STATIC)
  set (one_value_options)
  set (multi_value_options COMPONENTS)
  set (required_options COMPONENTS)
  parse_arguments_with_unknown (FIND "${options}" "${one_value_options}" "${multi_value_options}" "${required_options}" ${ARGN})

  if (FIND_FROM_GPISPACE_INSTALLATION)
    if (NOT GSPC_HOME)
      message (FATAL_ERROR
        "GSPC_HOME not set but FROM_GPISPACE_INSTALLATION is turned on"
      )
    endif()

    set (BOOST_ROOT "${GSPC_HOME}/external/boost")
  endif()

  if (FIND_STATIC)
    set (Boost_USE_STATIC_LIBS ON)
    set (Boost_USE_STATIC_RUNTIME ON)
  endif()

  find_package (Boost ${FIND_UNPARSED_ARGUMENTS} COMPONENTS ${FIND_COMPONENTS})

  extended_add_library (NAME base
    NAMESPACE Boost
    SYSTEM_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR}
  )

  foreach (component ${FIND_COMPONENTS})
    string (TOUPPER ${component} UPPERCOMPONENT)
    if (Boost_${UPPERCOMPONENT}_FOUND)
      set (additional_libraries Boost::base)
      if (${component} STREQUAL filesystem)
        list (APPEND additional_libraries Boost::system)
      endif()
      if (${component} STREQUAL thread)
        find_package (Threads REQUIRED)

        list (APPEND additional_libraries Threads::Threads)
        #! \todo find librt, only link it when required
        list (APPEND additional_libraries rt)
      endif()
      extended_add_library (NAME ${component}
        NAMESPACE Boost
        LIBRARIES ${Boost_${UPPERCOMPONENT}_LIBRARIES} ${additional_libraries}
      )
    endif()
  endforeach()
endmacro()

macro (bundle_boost)
  set (options)
  set (one_value_options DESTINATION)
  set (multi_value_options)
  set (required_options DESTINATION)
  parse_arguments (ARG "${options}" "${one_value_options}" "${multi_value_options}" "${required_options}" ${ARGN})

  get_filename_component (BOOST_ROOT "${Boost_INCLUDE_DIR}" PATH)

  install (DIRECTORY "${BOOST_ROOT}/"
    DESTINATION "${ARG_DESTINATION}"
    USE_SOURCE_PERMISSIONS
  )
endmacro()
