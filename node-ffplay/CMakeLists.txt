
# cmake -G "Visual Studio 15" -A x64 .. -DFFMPEG_SRC_DIR=...
# cmake --build . --config Release

cmake_minimum_required(VERSION 3.9)
# Every project needs a name.  We call this the "examples" project.
project(ff_binding)

option(FFMPEG_SRC_DIR "ffmpeg source directory")

set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")


if (NOT NODE_MODULE_MINIMUM_ABI)
  set(NODE_MODULE_MINIMUM_ABI 67) # Don't build node modules for versions earlier than Node 10
endif()

if (NOT NODE_ADDON_API)
  set(NODE_ADDON_API 1.7.1)
endif()

if (NOT NODE_MODULE_CACHE_DIR)
    set(NODE_MODULE_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/node_cache")
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
    cmake_parse_arguments("" "" "NAPI_VERSION" "EXCLUDE_NODE_ABIS" ${ARGN})
    if(NOT _NAPI_VERSION)
        set(_NAPI_VERSION "${NODE_ADDON_API}")
    endif()
    if(_UNPARSED_ARGUMENTS)
        message(WARNING "[Node.js] Unused arguments: '${_UNPARSED_ARGUMENTS}'")
    endif()

    # Create master target
    add_library(${NAME} INTERFACE)

    # Obtain a list of current Node versions and retrieves the latest version per ABI
    if(NOT EXISTS "${NODE_MODULE_CACHE_DIR}/node/index.tab")
        _node_module_download(
            "https://nodejs.org/dist/index.tab"
            "${NODE_MODULE_CACHE_DIR}/node/index.tab"
        )
    endif()
    file(STRINGS "${NODE_MODULE_CACHE_DIR}/node/index.tab" _INDEX_FILE)
    list(REMOVE_AT _INDEX_FILE 0)
    set(_ABIS)
    foreach(_LINE IN LISTS _INDEX_FILE)
        string(REGEX MATCHALL "[^\t]*\t" _COLUMNS "${_LINE}")
        list(GET _COLUMNS 8 _ABI)
        string(STRIP "${_ABI}" _ABI)
        if((_ABI GREATER NODE_MODULE_MINIMUM_ABI OR _ABI EQUAL NODE_MODULE_MINIMUM_ABI) AND NOT _ABI IN_LIST _EXCLUDE_NODE_ABIS AND NOT DEFINED _NODE_ABI_${_ABI}_VERSION)
            list(APPEND _ABIS ${_ABI})
            list(GET _COLUMNS 0 _VERSION)
            string(STRIP "${_VERSION}" _NODE_ABI_${_ABI}_VERSION)
        endif()
    endforeach()


    # Install node-addon-api
    if(NOT EXISTS "${NODE_MODULE_CACHE_DIR}/node-addon-api/${_NAPI_VERSION}/napi.h")
        _node_module_unpack_tar_gz(
            "https://registry.npmjs.org/node-addon-api/-/node-addon-api-${_NAPI_VERSION}.tgz"
            "package"
            "${NODE_MODULE_CACHE_DIR}/node-addon-api/${_NAPI_VERSION}"
        )
    endif()


    # Generate a target for every ABI
    set(_TARGETS)
    foreach(_ABI IN LISTS _ABIS)
        set(_NODE_VERSION ${_NODE_ABI_${_ABI}_VERSION})

        # Download the headers if we don't have them yet
        if(NOT EXISTS "${NODE_MODULE_CACHE_DIR}/node/${_NODE_VERSION}/node.h")
            _node_module_unpack_tar_gz(
                "https://nodejs.org/download/release/${_NODE_VERSION}/node-${_NODE_VERSION}-headers.tar.gz"
                "node-${_NODE_VERSION}/include/node"
                "${NODE_MODULE_CACHE_DIR}/node/${_NODE_VERSION}"
            )
        endif()

        if (WIN32)
          if (CMAKE_CL_64)
            set(ARCHSUFFIX "win32-x64")
            set(LIB_DIST_URL "https://nodejs.org/dist/${_NODE_VERSION}/win-x64/node.lib")
            set(LIB_DIST "${NODE_MODULE_CACHE_DIR}/node/${_NODE_VERSION}/win-x64/node.lib")
          else()
            set(ARCHSUFFIX "win32-x86")
            set(LIB_DIST_URL "https://nodejs.org/dist/${_NODE_VERSION}/win-x86/node.lib")
            set(LIB_DIST "${NODE_MODULE_CACHE_DIR}/node/${_NODE_VERSION}/win-x86/node.lib")
          endif()
        else()
          set(ARCHSUFFIX "linux-x64")
        endif()

        if(WIN32 AND NOT EXISTS "${LIB_DIST}")
          _node_module_download(
              "${LIB_DIST_URL}"
              "${LIB_DIST}"
          )
        endif()

        # Generate the library
        set(_TARGET "${NAME}-${ARCHSUFFIX}.abi-${_ABI}")
        add_library(${_TARGET} SHARED "${NODE_MODULE_CACHE_DIR}/empty.cpp")
        list(APPEND _TARGETS "${_TARGET}")

        # C identifiers can only contain certain characters (e.g. no dashes)
        string(REGEX REPLACE "[^a-z0-9]+" "_" NAME_IDENTIFIER "${NAME}")

        set_target_properties(${_TARGET} PROPERTIES
            OUTPUT_NAME "${_TARGET}"
            SOURCES "" # Removes the fake empty.cpp again
            PREFIX ""
            SUFFIX ".node"
            MACOSX_RPATH ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            POSITION_INDEPENDENT_CODE TRUE
        )

        set_target_properties(${_TARGET} PROPERTIES COMPILE_FLAGS "/ignore:4090")

        target_compile_definitions(${_TARGET} PRIVATE
            "MODULE_NAME=${NAME_IDENTIFIER}"
            "BUILDING_NODE_EXTENSION"
            "_LARGEFILE_SOURCE"
            "_FILE_OFFSET_BITS=64"
        )

        target_include_directories(${_TARGET} SYSTEM PRIVATE
            "${NODE_MODULE_CACHE_DIR}/node/${_NODE_VERSION}"
        )

        target_include_directories(${_TARGET} SYSTEM PRIVATE
          "${NODE_MODULE_CACHE_DIR}/node-addon-api/${_NAPI_VERSION}"
        )

        target_link_libraries(${_TARGET} PRIVATE ${NAME})
        if (WIN32)
            target_link_libraries(${_TARGET} PRIVATE ${LIB_DIST})
        endif()

        if (MSVC)
          set_target_properties(${_TARGET} PROPERTIES COMPILE_FLAGS "/EHsc")
          set_target_properties(${_TARGET} PROPERTIES LINK_FLAGS "/DELAYLOAD:node.exe /DELAYLOAD:iojs.exe")
        endif()
    endforeach()
endfunction()


# build nodejs module
add_node_module(ff_binding)

target_sources(ff_binding INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/win_delay_load_hook.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/wrap.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/player.cc
)

target_link_directories(ff_binding INTERFACE
    ${FFMPEG_SRC_DIR}/../../msvc/lib/x64)

target_include_directories(ff_binding INTERFACE
    ${FFMPEG_SRC_DIR}/../../msvc/include
    ${FFMPEG_SRC_DIR}
    ${FFMPEG_SRC_DIR}/SMP
    ${FFMPEG_SRC_DIR}/libavutil)
target_compile_definitions(ff_binding INTERFACE BUILD_NODEJS WIN32 _WINDOWS _USE_MATH_DEFINES HAVE_AV_CONFIG_H _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0600 NDEBUG)
target_compile_definitions(ff_binding INTERFACE NAPI_CPP_EXCEPTIONS) # NAPI_DISABLE_CPP_EXCEPTIONS
#target_compile_definitions(ff_binding INTERFACE BUILD_WITH_AUDIO_FILTER) #  BUILD_WITH_VIDEO_FILTER

target_link_libraries(ff_binding INTERFACE 
    libavcodec
    libavutil
    libavformat
    libswresample
    libswscale
    #libavfilter
    libpostproc
    libsdl2)
