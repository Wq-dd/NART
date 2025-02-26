FUNCTION(GET_STATIC_TARGET_RECURSIVE OUT_VAR)
    FOREACH(ITEM ${ARGN})
        IF (TARGET ${ITEM})
            SET(LIBS)
            SET(IS_STATIC)
            GET_TARGET_PROPERTY(IS_IMPORTED ${ITEM} IMPORTED)
            GET_TARGET_PROPERTY(INTERFACE_LINKS ${ITEM} INTERFACE_LINK_LIBRARIES)
            GET_TARGET_PROPERTY(TP ${ITEM} TYPE)
            IF ("${TP}" STREQUAL "STATIC_LIBRARY")
                SET(IS_STATIC TRUE)
            ENDIF ()
            IF (NOT "${TP}" STREQUAL "INTERFACE_LIBRARY")
                GET_TARGET_PROPERTY(LIBS ${ITEM} LINK_LIBRARIES)
            ENDIF()

            GET_STATIC_TARGET_RECURSIVE(${OUT_VAR} ${INTERFACE_LINKS})
            GET_STATIC_TARGET_RECURSIVE(${OUT_VAR} ${LIBS})
            
            IF (IS_STATIC)
                LIST(APPEND ${OUT_VAR} ${ITEM})
                LIST(REMOVE_DUPLICATES ${OUT_VAR})
            ENDIF ()
        ENDIF ()
    ENDFOREACH()
    set(${OUT_VAR} ${${OUT_VAR}} PARENT_SCOPE)
ENDFUNCTION()

macro(find_llvm use_llvm)
  if(${use_llvm} MATCHES ${IS_FALSE_PATTERN})
    return()
  endif()
  set(LLVM_CONFIG ${use_llvm})
  if(${ARGC} EQUAL 2)
    set(llvm_version_required ${ARGV1})
  endif()

  if(${LLVM_CONFIG} MATCHES ${IS_TRUE_PATTERN})
    find_package(LLVM ${llvm_version_required} REQUIRED CONFIG)
    llvm_map_components_to_libnames(LLVM_LIBS "all")
    if (NOT LLVM_LIBS)
      message(STATUS "Not found - LLVM_LIBS")
      message(STATUS "Fall back to using llvm-config")
      set(LLVM_CONFIG "${LLVM_TOOLS_BINARY_DIR}/llvm-config")
    endif()
  endif()

  if(LLVM_LIBS) # check if defined, not if it is true
    list (FIND LLVM_LIBS "LLVM" _llvm_dynlib_index)
    if (${_llvm_dynlib_index} GREATER -1)
      set(LLVM_LIBS LLVM)
      message(STATUS "Link with dynamic LLVM library")
    else()
      list(REMOVE_ITEM LLVM_LIBS LTO)
      message(STATUS "Link with static LLVM libraries")
    endif()
    set(TVM_LLVM_VERSION ${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR})
    set(TVM_INFO_LLVM_VERSION "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")
  else()
    # use llvm config
    message(STATUS "Use llvm-config=" ${LLVM_CONFIG})
    separate_arguments(LLVM_CONFIG)
    execute_process(COMMAND ${LLVM_CONFIG} --libfiles
      RESULT_VARIABLE __llvm_exit_code
      OUTPUT_VARIABLE __llvm_libfiles_space
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${__llvm_exit_code}" STREQUAL "0")
      message(FATAL_ERROR "Fatal error executing: ${use_llvm} --libfiles")
    endif()
    execute_process(COMMAND ${LLVM_CONFIG} --system-libs
      RESULT_VARIABLE __llvm_exit_code
      OUTPUT_VARIABLE __llvm_system_libs
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${__llvm_exit_code}" STREQUAL "0")
      message(FATAL_ERROR "Fatal error executing: ${use_llvm} --system-libs")
    endif()
    execute_process(COMMAND ${LLVM_CONFIG} --cxxflags
      RESULT_VARIABLE __llvm_exit_code
      OUTPUT_VARIABLE __llvm_cxxflags_space
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${__llvm_exit_code}" STREQUAL "0")
      message(FATAL_ERROR "Fatal error executing: ${use_llvm} --cxxflags")
    endif()
    execute_process(COMMAND ${LLVM_CONFIG} --version
      RESULT_VARIABLE __llvm_exit_code
      OUTPUT_VARIABLE __llvm_version
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${__llvm_exit_code}" STREQUAL "0")
      message(FATAL_ERROR "Fatal error executing: ${use_llvm} --version")
    endif()
    execute_process(COMMAND ${LLVM_CONFIG} --prefix
      RESULT_VARIABLE __llvm_exit_code
      OUTPUT_VARIABLE __llvm_prefix
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${__llvm_exit_code}" STREQUAL "0")
      message(FATAL_ERROR "Fatal error executing: ${use_llvm} --prefix")
    endif()
    execute_process(COMMAND ${LLVM_CONFIG} --libdir
      RESULT_VARIABLE __llvm_exit_code
      OUTPUT_VARIABLE __llvm_libdir
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT "${__llvm_exit_code}" STREQUAL "0")
      message(FATAL_ERROR "Fatal error executing: ${use_llvm} --libdir")
    endif()
    # map prefix => $
    # to handle the case when the prefix contains space.
    string(REPLACE ${__llvm_prefix} "$" __llvm_cxxflags ${__llvm_cxxflags_space})
    string(REPLACE ${__llvm_prefix} "$" __llvm_libfiles ${__llvm_libfiles_space})
    # llvm version
    set(TVM_INFO_LLVM_VERSION ${__llvm_version})
    string(REGEX REPLACE "^([^.]+)\.([^.])+\.[^.]+.*$" "\\1\\2" TVM_LLVM_VERSION ${__llvm_version})
    string(STRIP ${TVM_LLVM_VERSION} TVM_LLVM_VERSION)
    # definitions
    string(REGEX MATCHALL "(^| )-D[A-Za-z0-9_]*" __llvm_defs ${__llvm_cxxflags})
    set(LLVM_DEFINITIONS "")
    foreach(__flag IN ITEMS ${__llvm_defs})
      string(STRIP "${__flag}" __llvm_def)
      list(APPEND LLVM_DEFINITIONS "${__llvm_def}")
    endforeach()
    # include dir
    string(REGEX MATCHALL "(^| )-I[^ ]*" __llvm_include_flags ${__llvm_cxxflags})
    set(LLVM_INCLUDE_DIRS "")
    foreach(__flag IN ITEMS ${__llvm_include_flags})
      string(REGEX REPLACE "(^| )-I" "" __dir "${__flag}")
      # map $ => prefix
      string(REPLACE "$" ${__llvm_prefix} __dir_with_prefix "${__dir}")
      list(APPEND LLVM_INCLUDE_DIRS "${__dir_with_prefix}")
    endforeach()
    # libfiles
    set(LLVM_LIBS "")
    separate_arguments(__llvm_libfiles)
    foreach(__flag IN ITEMS ${__llvm_libfiles})
      # map $ => prefix
      string(REPLACE "$" ${__llvm_prefix} __lib_with_prefix "${__flag}")
      list(APPEND LLVM_LIBS "${__lib_with_prefix}")
    endforeach()
    separate_arguments(__llvm_system_libs)
    foreach(__flag IN ITEMS ${__llvm_system_libs})
      # If the library file ends in .lib try to
      # also search the llvm_libdir
      if(__flag MATCHES ".lib$")
        if(EXISTS "${__llvm_libdir}/${__flag}")
          set(__flag "${__llvm_libdir}/${__flag}")
        endif()
      endif()
      list(APPEND LLVM_LIBS "${__flag}")
    endforeach()
  endif()
  message(STATUS "Found LLVM_INCLUDE_DIRS=" "${LLVM_INCLUDE_DIRS}")
  message(STATUS "Found LLVM_DEFINITIONS=" "${LLVM_DEFINITIONS}")
  message(STATUS "Found LLVM_LIBS=" "${LLVM_LIBS}")
  message(STATUS "Found TVM_LLVM_VERSION=" ${TVM_LLVM_VERSION})
  if (${TVM_LLVM_VERSION} LESS 40)
    message(FATAL_ERROR "TVM requires LLVM 4.0 or higher.")
  endif()
endmacro(find_llvm)
