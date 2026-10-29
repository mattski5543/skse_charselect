cmake_minimum_required(VERSION 3.26)

set(PROJECT_AUTHOR "portalfl0w")
project(
	CharSelect
	VERSION 0.9.14
	LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed. Use a preset (cmake --preset release).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# CommonLibSSE-NG (alandtse fork), built from the pinned submodule.
# SE + AE in one DLL via Address Library. VR is off: not a target, and it drags
# in OpenVR plus a longer CommonLib compile.
# ------------------------------------------------------------------------------
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG EXCLUDE_FROM_ALL)
# CommonLib only pulls in its add_commonlibsse_plugin() helper on the prebuilt
# path, so include it directly for the source build.
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

# ------------------------------------------------------------------------------
# Plugin
# ------------------------------------------------------------------------------
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.h")

# Generates SKSEPlugin_Version / SKSEPlugin_Query (name, version, author,
# Address Library runtime compatibility) and links CommonLibSSE.
add_commonlibsse_plugin(${PROJECT_NAME}
	AUTHOR "${PROJECT_AUTHOR}"
	SOURCES ${PLUGIN_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_precompile_headers(${PROJECT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/src/PCH.h")
# Includes <windows.h>, which clashes with CommonLib's own Win32 declarations.
set_source_files_properties("${PROJECT_SOURCE_DIR}/src/core/Encoding.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

# Offline test harness: the plugin's save indexer as a console exe, so header
# parsing can be checked against real saves without launching the game.
add_executable(savescan tools/savescan.cpp src/SaveIndex.cpp src/core/Encoding.cpp)
target_include_directories(savescan PRIVATE "${PROJECT_SOURCE_DIR}/src")
if(MSVC)
	target_compile_options(savescan PRIVATE /W4 /utf-8 /permissive-)
endif()

# ------------------------------------------------------------------------------
# Unit tests: the game-free code (src/core, SaveIndex) built into a console exe
# with Catch2, plus the Python tools' tests. Run with: ctest --preset release
# ------------------------------------------------------------------------------
option(CHARSELECT_TESTS "Build the unit tests" ON)
if(CHARSELECT_TESTS)
	enable_testing()
	find_package(Catch2 3 CONFIG REQUIRED)
	file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/core/*.cpp")
	file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/*.cpp" "${PROJECT_SOURCE_DIR}/tests/*.h")
	add_executable(CharSelectTests ${TEST_SOURCES} ${CORE_SOURCES} src/SaveIndex.cpp)
	target_include_directories(CharSelectTests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
	target_link_libraries(CharSelectTests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
	target_compile_definitions(CharSelectTests PRIVATE CHARSELECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
	if(MSVC)
		target_compile_options(CharSelectTests PRIVATE /W4 /utf-8 /permissive-)
	endif()
	include(Catch)
	catch_discover_tests(CharSelectTests)

	find_package(Python3 COMPONENTS Interpreter)
	if(Python3_FOUND)
		add_test(NAME python-tools
			COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tools/tests -v
			WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
	endif()
endif()

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8 /Zc:preprocessor /permissive-)
	# Always emit a PDB so crash loggers can symbolicate our frames.
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL)
endif()

# ------------------------------------------------------------------------------
# Output: dist/<name>-<version>.zip, a FOMOD installer archive for Vortex/MO2:
#   Core/       the plugin (+ .pdb so crash loggers can resolve our frames),
#               the layouts and the menu movie, always installed
#   Locations/  one CharSelect.ini per select screen location, one installed
#   fomod/      the installer (fomod/ in the repo; see cmake/StageFomod.cmake)
# Optionally also copy straight into a mod folder by configuring with
# -DDEPLOY_DIR="<path to mod folder>".
# ------------------------------------------------------------------------------
set(STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/package")
set(DIST_ZIP "${PROJECT_SOURCE_DIR}/dist/${PROJECT_NAME}-${PROJECT_VERSION}.zip")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E remove_directory "${STAGE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/Core/SKSE/Plugins" "${PROJECT_SOURCE_DIR}/dist"
	# Loose mod files (INIs, Interface\CharSelectMenu.swf) ship as-is, except
	# CharSelect.ini, which comes from the chosen location.
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/dist-files" "${STAGE_DIR}/Core"
	COMMAND ${CMAKE_COMMAND} -E rm -f "${STAGE_DIR}/Core/SKSE/Plugins/CharSelect.ini"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${STAGE_DIR}/Core/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}> "${STAGE_DIR}/Core/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DSTAGE_DIR=${STAGE_DIR}
		-DPROJECT_VERSION=${PROJECT_VERSION} -DPROJECT_AUTHOR=${PROJECT_AUTHOR}
		-P "${PROJECT_SOURCE_DIR}/cmake/StageFomod.cmake"
	COMMENT "Staging ${PROJECT_NAME} package")
# Separate step: WORKING_DIRECTORY must already exist when the step starts.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E rm -f "${DIST_ZIP}"
	COMMAND ${CMAKE_COMMAND} -E tar cf "${DIST_ZIP}" --format=zip -- fomod Core Locations
	WORKING_DIRECTORY "${STAGE_DIR}"
	COMMENT "Packaging ${DIST_ZIP}")

set(DEPLOY_DIR "" CACHE PATH "Optional mod folder to copy SKSE/Plugins output into")
if(DEPLOY_DIR)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}> "${DEPLOY_DIR}/SKSE/Plugins/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PDB_FILE:${PROJECT_NAME}> "${DEPLOY_DIR}/SKSE/Plugins/"
		COMMENT "Deploying ${PROJECT_NAME} to ${DEPLOY_DIR}")
endif()
