# Might be reviewed
cmake_minimum_required(VERSION 3.3)

# Set for latter use
set(CORE_DIR ${CMAKE_CURRENT_LIST_DIR})

# Registers a project
macro(register_project project_name)
	# This check is intentionally copied from core's listfile
	# Reason is that if build API used a proper platform must be set
	# _before_ compiler definitions and core inclusion.
	if (NOT DEFINED CONFIG_PLATFORM)
		message(FATAL_ERROR "CONFIG_PLATFORM must be set in order to use valid platform")
	else()
		message(STATUS "Platform will be used: ${CONFIG_PLATFORM}")
		set(PLATFORM_NAME ${CONFIG_PLATFORM}) # For convinience
	endif()

	set(PROJECT_DIR ${CMAKE_CURRENT_DIR}/${project_name})
	set(PLATFORM_DIR ${CORE_DIR}/platform/${PLATFORM_NAME})

	# Pick common compiler definitions for given platform
	include(${CORE_DIR}/platform/common/compiler.cmake)

	# Make sure the core is included
	add_subdirectory(${CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/core)
endmacro()

# Creates a host unit test
function(add_unit_host_test test_name test_sources)
	# Add test only if not cross-compiling
	if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL ${CMAKE_SYSTEM_NAME})
		add_executable(${test_name} ${test_sources} ${ARGN})
		target_link_libraries(${test_name} CppUTest)
		add_test(${test_name} ${test_name})
	endif()
endfunction()
