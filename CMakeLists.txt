# --- Detect if the plugin is build out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.16...3.26)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${PROJECT_NAME} MODULE)

if(BUILD_OUT_OF_TREE)
  find_package(libobs)
  if(libobs_FOUND AND NOT LIBOBS_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)
  else()
    if(NOT LIBOBS_LIB)
      message(FATAL_ERROR "obs library not found - please set LIBOBS_LIB")
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBOBS_LIB})
    if(NOT LIBOBS_INCLUDE_DIR)
      message(FATAL_ERROR "obs.hpp header not found - please set LIBOBS_INCLUDE_DIR")
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBOBS_INCLUDE_DIR})
  endif()
  find_package(obs-frontend-api)
  if(obs-frontend-api_FOUND AND NOT LIBOBS_FRONTEND_API_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  else()
    if(NOT LIBOBS_FRONTEND_API_LIB)
      message(FATAL_ERROR "libobs frontend-api library not found - please set LIBOBS_FRONTEND_API_LIB")
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBOBS_FRONTEND_API_LIB})
    if(NOT LIBOBS_FRONTEND_INCLUDE_DIR)
      message(FATAL_ERROR " obs-frontend-api.h not found - please set LIBOBS_FRONTEND_INCLUDE_DIR")
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBOBS_FRONTEND_INCLUDE_DIR})
  endif()
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs OBS::frontend-api)
endif()

if(BUILD_OUT_OF_TREE)
  find_qt(COMPONENTS Widgets Core)
else()
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
target_compile_options(
${PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                -Wno-comma>)
#set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(
${PROJECT_NAME}
PROPERTIES AUTOMOC ON
            AUTOUIC ON
            AUTORCC ON)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

target_sources(${PROJECT_NAME} PRIVATE
  source-profiler.cpp
  source-profiler.hpp
	version.h)

if(BUILD_OUT_OF_TREE)
	set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
else()
	set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER "plugins/exeldro" PREFIX "")
endif()
