# Copyright (c) OpenMMLab. All rights reserved.

if (NOT DEFINED TENSORRT_DIR)
    set(TENSORRT_DIR $ENV{TENSORRT_DIR})
endif ()
if (NOT TENSORRT_DIR)
    message(FATAL_ERROR "Please set TENSORRT_DIR with cmake -D option.")
endif()

find_path(
    TENSORRT_INCLUDE_DIR NvInfer.h
    HINTS ${TENSORRT_DIR} ${CUDA_TOOLKIT_ROOT_DIR}
    PATH_SUFFIXES include)

if (NOT TENSORRT_INCLUDE_DIR)
    message(FATAL_ERROR "Cannot find TensorRT header NvInfer.h "
        "in TENSORRT_DIR: ${TENSORRT_DIR} or in CUDA_TOOLKIT_ROOT_DIR: "
        "${CUDA_TOOLKIT_ROOT_DIR}, please check if the path is correct.")
endif ()

file(READ "${TENSORRT_DIR}/include/NvInferVersion.h" TENSORRT_VERSION_FILE)
string(REGEX MATCH "#define NV_TENSORRT_MAJOR ([0-9]+)" _ ${TENSORRT_VERSION_FILE})
set(TensorRT_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "#define NV_TENSORRT_MINOR ([0-9]+)" _ ${TENSORRT_VERSION_FILE})
set(TensorRT_VERSION_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "#define NV_TENSORRT_PATCH ([0-9]+)" _ ${TENSORRT_VERSION_FILE})
set(TensorRT_VERSION_PATCH ${CMAKE_MATCH_1})

set(TensorRT_VERSION "${TensorRT_VERSION_MAJOR}.${TensorRT_VERSION_MINOR}.${TensorRT_VERSION_PATCH}")
message(STATUS "TensorRT version: ${TensorRT_VERSION}")

if(TensorRT_VERSION VERSION_LESS "10.0" OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "TensorRT version is less than 10.0 or using Linux")
    set(__TENSORRT_LIB_COMPONENTS nvinfer;nvinfer_plugin)
else()
    message(STATUS "TensorRT version is 8.0 or greater")
    set(__TENSORRT_LIB_COMPONENTS nvinfer_10;nvinfer_plugin_10)

endif()
foreach(__component ${__TENSORRT_LIB_COMPONENTS})
    find_library(
        __component_path ${__component}
        HINTS ${TENSORRT_DIR} ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES lib lib64 lib/x64)
    if (NOT __component_path)
        message(FATAL_ERROR "Cannot find TensorRT lib ${__component} in "
            "TENSORRT_DIR: ${TENSORRT_DIR} or CUDA_TOOLKIT_ROOT_DIR: ${CUDA_TOOLKIT_ROOT_DIR}, "
            "please check if the path is correct")
    endif()

    add_library(${__component} SHARED IMPORTED)
    set_property(TARGET ${__component} APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    if (MSVC)
        set_target_properties(
            ${__component} PROPERTIES
            IMPORTED_IMPLIB_RELEASE ${__component_path}
            INTERFACE_INCLUDE_DIRECTORIES ${TENSORRT_INCLUDE_DIR}
        )
    else()
        set_target_properties(
            ${__component} PROPERTIES
            IMPORTED_LOCATION_RELEASE ${__component_path}
            INTERFACE_INCLUDE_DIRECTORIES ${TENSORRT_INCLUDE_DIR}
        )
    endif()
    unset(__component_path CACHE)
endforeach()

set(TENSORRT_LIBS ${__TENSORRT_LIB_COMPONENTS})
