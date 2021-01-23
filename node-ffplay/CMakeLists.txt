
# cmake -G "Visual Studio 15" -A x64 .. -DFFMPEG_PREBUILD_DIR=...
# cmake --build . --config Release

cmake_minimum_required(VERSION 3.9)

project(node-ffplay)

set(THIRD_PARTY_PREBUILT ${THIRD_PARTY_DIR}/prebuilt)

if (NOT FFMPEG_PREBUILD_DIR)
  set(FFMPEG_PREBUILD_DIR ${THIRD_PARTY_PREBUILT})
endif()

set(FFMPEG_INCLUDE_PATH ${FFMPEG_PREBUILD_DIR}/include)
set(FFMPEG_LIB_PATH ${FFMPEG_PREBUILD_DIR}/lib)

set(THIRD_INC_PATH ${THIRD_PARTY_PREBUILT}/include)
if (CMAKE_CL_64)
  set(THIRD_LIB_PATH ${THIRD_PARTY_PREBUILT}/lib/x64)
else()
  set(THIRD_LIB_PATH ${THIRD_PARTY_PREBUILT}/lib/x86)
endif()

include(../cmake-tool/node-addon.cmake)

# build nodejs module
add_node_module(node-ffplay 
  CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../node_cache"
  SUFFIX ".dll"
  NODE_ABIS ${NODE_ABI})

list(GET node-ffplay::targets 0 _TARGET)

target_sources(node-ffplay INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/wrap.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/player.cc
)

if(MSVC)
    target_compile_options(node-ffplay INTERFACE /W3 /WX- 
      /wd"4005"
      /wd"4018"
      /wd"4047"
      /wd"4090"
      /wd"4101"
      /wd"4146"
      /wd"4229"
      /wd"4244"
      /wd"4267"
      /wd"4305"
      /wd"4334"
      /wd"4819"
      /wd"4996")
endif()

target_link_directories(node-ffplay INTERFACE
    ${FFMPEG_LIB_PATH}
    ${THIRD_LIB_PATH})

target_include_directories(node-ffplay INTERFACE
    ${FFMPEG_INCLUDE_PATH}
    ${THIRD_INC_PATH})
target_compile_definitions(node-ffplay INTERFACE BUILD_NODEJS WIN32 _WINDOWS _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0600 NDEBUG)
target_compile_definitions(node-ffplay INTERFACE NAPI_CPP_EXCEPTIONS) # NAPI_DISABLE_CPP_EXCEPTIONS
#target_compile_definitions(node-ffplay INTERFACE BUILD_WITH_AUDIO_FILTER) #  BUILD_WITH_VIDEO_FILTER

target_link_libraries(node-ffplay INTERFACE 
    avcodec
    avutil
    avformat
    swresample
    swscale
    avfilter
    sdl2)

add_custom_target(CopyRuntimeFiles ALL 
    VERBATIM 
    COMMAND_EXPAND_LISTS 
    COMMAND ${CMAKE_COMMAND} -E 
      copy_if_different
                $<TARGET_FILE:${_TARGET}>
                "${CMAKE_CURRENT_SOURCE_DIR}/../"   
)
