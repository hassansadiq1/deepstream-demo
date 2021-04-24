cmake_minimum_required(VERSION 3.4)
project(main_app)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -g3")

######### SET CMAKE #########
set(CMAKE_CONFIGURATION_TYPES "Debug" CACHE STRING "" FORCE)
######### SET CMAKE END #########

include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/app_main.cpp"
                  "${PROJECT_SOURCE_DIR}/src/pipeline.cpp"
                  "${PROJECT_SOURCE_DIR}/src/json.cpp"
                  "${PROJECT_SOURCE_DIR}/src/common.cpp"
                  )

find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)
MESSAGE(STATUS "GST= ${GST_LIBRARIES}")
include_directories(${GST_INCLUDE_DIRS})
link_libraries(${GST_LIBRARIES})
link_libraries(gstrtspserver-1.0)
link_libraries(gstrtp-1.0)

FIND_PACKAGE(X11 REQUIRED)
MESSAGE(STATUS "X11= ${X11_LIBRARIES}")
link_libraries(${X11_LIBRARIES})

include_directories(/opt/nvidia/deepstream/deepstream/sources/includes)
include_directories(/opt/nvidia/deepstream/deepstream/sources/apps/apps-common/includes)
set(CMAKE_PREFIX_PATH /opt/nvidia/deepstream/deepstream/lib)
link_directories(/opt/nvidia/deepstream/deepstream/lib)
find_library(NVDS_LIBRARIES NAMES nvdsgst_meta nvds_meta nvdsgst_helper nvds_utils)
MESSAGE(STATUS "NVDS= ${NVDS_LIBRARIES}")
link_libraries(${NVDS_LIBRARIES})
link_libraries(nvdsgst_meta)
link_libraries(nvds_meta)
link_libraries(nvdsgst_helper)
link_libraries(nvds_utils)
link_libraries(rdkafka++)

add_executable(app_main ${SOURCES})
target_link_libraries(app_main ${libs_collection})
target_link_libraries(app_main  PUBLIC rdkafka++ ssl crypto pthread rdkafka)