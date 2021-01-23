if (NOT NODE_MODULE_CACHE_DIR)
    set(NODE_MODULE_CACHE_DIR "${CMAKE_BINARY_DIR}")
endif()

if (NOT NODE_ADDON_API)
  set(NODE_ADDON_API 1.7.1)
endif()

if (NOT NODE_NAN_API)
  set(NODE_NAN_API 2.14.1)
endif()


if (WIN32)
  if (CMAKE_CL_64)
    set(ARCHSUFFIX "win32-x64")
  else()
    set(ARCHSUFFIX "win32-x86")
  endif()
else()
  set(ARCHSUFFIX "linux-x64")
endif()

function(_node_module_download _URL _FILE)
    get_filename_component(_DIR "${_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_DIR}")
    message(STATUS "[Node.js] Downloading ${_URL}...")
    file(DOWNLOAD "${_URL}" "${_FILE}" STATUS _STATUS TLS_VERIFY ON)
    list(GET _STATUS 0 _STATUS_CODE)
    if(NOT _STATUS_CODE EQUAL 0)
        list(GET _STATUS 1 _STATUS_MESSAGE)
        message(FATAL_ERROR "[Node.js] Failed to download ${_URL}: ${_STATUS_MESSAGE}")
    endif()
endfunction()

function(_node_module_unpack_tar_gz _URL _PATH _DEST)
    string(RANDOM LENGTH 32 _TMP)
    set(_TMP "${CMAKE_BINARY_DIR}/${_TMP}")
    _node_module_download("${_URL}" "${_TMP}.tar.gz")
    file(REMOVE_RECURSE "${_DEST}" "${_TMP}")
    file(MAKE_DIRECTORY "${_TMP}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xfz "${_TMP}.tar.gz"
        WORKING_DIRECTORY "${_TMP}"
        RESULT_VARIABLE _STATUS_CODE
        OUTPUT_VARIABLE _STATUS_MESSAGE
        ERROR_VARIABLE _STATUS_MESSAGE)
    if(NOT _STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "[Node.js] Failed to unpack ${_URL}: ${_STATUS_MESSAGE}")
    endif()
    get_filename_component(_DIR "${_DEST}" DIRECTORY)
    file(MAKE_DIRECTORY "${_DIR}")
    file(RENAME "${_TMP}/${_PATH}" "${_DEST}")
    file(REMOVE_RECURSE "${_TMP}" "${_TMP}.tar.gz")
endfunction()


function(add_node_module NAME)
    cmake_parse_arguments("" "" "ADDON_VERSION;NAN_VERSION;CACHE_DIR;SUFFIX" "NODE_ABIS" ${ARGN})
    if (NOT _CACHE_DIR)
        set(_CACHE_DIR "${NODE_MODULE_CACHE_DIR}")
    endif()
    if (NOT _ADDON_VERSION)
        set(_ADDON_VERSION "${NODE_ADDON_API}")
    endif()
    if (NOT _SUFFIX)
        if (WIN32)
            set(_SUFFIX ".dll")
        else()
            set(_SUFFIX ".so")
        endif()
    endif()

    if(NOT _NODE_ABIS)
        message(FATAL_ERROR "No ABIs specified")
    endif()

    # Create master target
    add_library(${NAME} INTERFACE)

    # Obtain a list of current Node versions and retrieves the latest version per ABI
    if(NOT EXISTS "${_CACHE_DIR}/node/index.tab")
        _node_module_download(
            "Node.js version list"
            "https://nodejs.org/dist/index.tab"
            "${_CACHE_DIR}/node/index.tab"
        )
    endif()

    file(STRINGS "${_CACHE_DIR}/node/index.tab" _INDEX_FILE)
    list(REMOVE_AT _INDEX_FILE 0)

    set(_ABIS)
    foreach(_LINE IN LISTS _INDEX_FILE)
        string(REGEX MATCHALL "[^\t]*\t" _COLUMNS "${_LINE}")
        list(GET _COLUMNS 8 _ABI)
        string(STRIP "${_ABI}" _ABI)
        if(NOT DEFINED _NODE_ABI_${_ABI}_VERSION)
            list(APPEND _ABIS ${_ABI})
            list(GET _COLUMNS 0 _VERSION)
            string(STRIP "${_VERSION}" _NODE_ABI_${_ABI}_VERSION)
        endif()
    endforeach()

    # Install Nan
    if(_NAN_VERSION AND NOT EXISTS "${_CACHE_DIR}/nan/${_NAN_VERSION}/nan.h")
        _node_module_unpack_tar_gz(
            "Nan ${_NAN_VERSION}"
            "https://registry.npmjs.org/nan/-/nan-${_NAN_VERSION}.tgz"
            "package"
            "${_CACHE_DIR}/nan/${_NAN_VERSION}"
        )
    endif()

    # Install addon api
    if(_ADDON_VERSION AND NOT EXISTS "${_CACHE_DIR}/node-addon-api/${_ADDON_VERSION}/napi.h")
        _node_module_unpack_tar_gz(
            "https://registry.npmjs.org/node-addon-api/-/node-addon-api-${_ADDON_VERSION}.tgz"
            "package"
            "${_CACHE_DIR}/node-addon-api/${_ADDON_VERSION}"
        )
    endif()

    # Generate a target for every ABI
    set(_TARGETS)
    foreach(_ABI IN LISTS _NODE_ABIS)
        set(_NODE_VERSION ${_NODE_ABI_${_ABI}_VERSION})

        # Download the headers if we don't have them yet
        if(NOT EXISTS "${_CACHE_DIR}/node/${_NODE_VERSION}/node.h")
            _node_module_unpack_tar_gz(
                "headers for Node ${_NODE_VERSION}"
                "https://nodejs.org/download/release/${_NODE_VERSION}/node-${_NODE_VERSION}-headers.tar.gz"
                "node-${_NODE_VERSION}/include/node"
                "${_CACHE_DIR}/node/${_NODE_VERSION}"
            )
        endif()

        if (WIN32)
            if (CMAKE_CL_64)
                set(LIB_DIST_URL "https://nodejs.org/dist/${_NODE_VERSION}/win-x64/node.lib")
                set(LIB_DIST "${_CACHE_DIR}/node/${_NODE_VERSION}/win-x64/node.lib")
            else()
                set(LIB_DIST_URL "https://nodejs.org/dist/${_NODE_VERSION}/win-x86/node.lib")
                set(LIB_DIST "${_CACHE_DIR}/node/${_NODE_VERSION}/win-x86/node.lib")
            endif()

            if(NOT EXISTS "${LIB_DIST}")
                _node_module_download(
                    "${LIB_DIST_URL}"
                    "${LIB_DIST}"
                )
            endif()
        endif()

        # Generate the library
        set(_TARGET "${NAME}-${ARCHSUFFIX}.abi-${_ABI}")
        add_library(${_TARGET} SHARED "${_CACHE_DIR}/empty.cpp")
        list(APPEND _TARGETS "${_TARGET}")

        # C identifiers can only contain certain characters (e.g. no dashes)
        string(REGEX REPLACE "[^a-z0-9]+" "_" NAME_IDENTIFIER "${NAME}")

        set_target_properties(${_TARGET} PROPERTIES
            OUTPUT_NAME "${_TARGET}"
            SOURCES "${DELAY_LOAD_SRC}" # Removes the fake empty.cpp again
            PREFIX ""
            SUFFIX "${_SUFFIX}"
            MACOSX_RPATH ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            POSITION_INDEPENDENT_CODE TRUE
        )

        target_compile_definitions(${_TARGET} PRIVATE
            "MODULE_NAME=${NAME_IDENTIFIER}"
            "BUILDING_NODE_EXTENSION"
            "_LARGEFILE_SOURCE"
            "_FILE_OFFSET_BITS=64"
        )

        target_include_directories(${_TARGET} SYSTEM PRIVATE
            "${_CACHE_DIR}/node/${_NODE_VERSION}"
        )

        if(_NAN_VERSION)
            # target_compile_options(${_TARGET} PRIVATE -std=c++11)
            target_include_directories(${_TARGET} SYSTEM PRIVATE
                "${_CACHE_DIR}/nan/${_NAN_VERSION}"
            )
        endif()

        if(_ADDON_VERSION)
            # target_compile_options(${_TARGET} PRIVATE -std=c++11)
            target_include_directories(${_TARGET} SYSTEM PRIVATE
                "${_CACHE_DIR}/node-addon-api/${_ADDON_VERSION}"
            )
        endif()

        target_link_libraries(${_TARGET} PRIVATE ${NAME})
        if (WIN32)
            target_link_libraries(${_TARGET} PRIVATE ${LIB_DIST})
        endif()

        if(APPLE)
            # Ensures that linked symbols are loaded when the module is loaded instead of causing
            # unresolved symbol errors later during runtime.
            set_target_properties(${_TARGET} PROPERTIES
                LINK_FLAGS "-undefined dynamic_lookup -bind_at_load"
            )
            target_compile_definitions(${_TARGET} PRIVATE
                "_DARWIN_USE_64_BIT_INODE=1"
            )
        else()
            if (MSVC)
                set_target_properties(${_TARGET} PROPERTIES COMPILE_FLAGS "/EHsc")
                set_target_properties(${_TARGET} PROPERTIES LINK_FLAGS "/DELAYLOAD:node.exe /DELAYLOAD:iojs.exe")
            else()
                # Ensures that linked symbols are loaded when the module is loaded instead of causing
                # unresolved symbol errors later during runtime.
                set_target_properties(${_TARGET} PROPERTIES
                    LINK_FLAGS "-z now"
                )
            endif()
        endif()
    endforeach()

    # Add a target that builds all Node ABIs.
    add_custom_target("${NAME}.all")
    add_dependencies("${NAME}.all" ${_TARGETS})

    # Add a variable that allows users to iterate over all of the generated/dependendent targets.
    set("${NAME}::abis" "${_ABIS}" PARENT_SCOPE)
    set("${NAME}::targets" "${_TARGETS}" PARENT_SCOPE)
endfunction()
