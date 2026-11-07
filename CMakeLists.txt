cmake_minimum_required(VERSION 3.26)

set(PROJECT_AUTHOR "portalfl0w")
project(
	CharSelect
	VERSION 1.0.3
	LANGUAGES CXX)

# Test builds keep the release version and add a beta number: "b1" makes
# CharSelect-<version>-b1.zip, and the log and installer say <version>-b1.
# Empty for a release; the version itself only changes for one.
set(CHARSELECT_BETA "")
if(CHARSELECT_BETA)
	set(CHARSELECT_DISPLAY_VERSION "${PROJECT_VERSION}-${CHARSELECT_BETA}")
else()
	set(CHARSELECT_DISPLAY_VERSION "${PROJECT_VERSION}")
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed. Use a preset (cmake --preset release).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No paths from the build machine in what ships: source file names in the DLL
# (__FILE__, std::source_location) are trimmed to the repository, debug info
# maps the repository to C:\CharSelect, and the DLL names its PDB without a
# folder. Set before CommonLib so its objects get the same.
if(MSVC)
	file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}" CHARSELECT_NATIVE_DIR)
	add_compile_options(/experimental:deterministic "/d1trimfile:${CHARSELECT_NATIVE_DIR}\\" "/pathmap:${CHARSELECT_NATIVE_DIR}=C:\\CharSelect")
	add_link_options(/PDBALTPATH:%_PDB%)
endif()

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
target_compile_definitions(${PROJECT_NAME} PRIVATE CHARSELECT_DISPLAY_VERSION="${CHARSELECT_DISPLAY_VERSION}")
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
	# Always emit a PDB so crash loggers can symbolicate our frames: the full
	# one for debugging, and a stripped one (function names only) to ship.
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL "/PDBSTRIPPED:$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.public.pdb")
endif()

# ------------------------------------------------------------------------------
# Output: dist/<name>-<version>.zip, a FOMOD installer archive for Vortex/MO2:
#   Core/       the plugin (+ the stripped .pdb, so crash loggers can name our functions),
#               the layouts and the menu movie, always installed
#   Locations/  one CharSelect.ini per location and main menu choice, one installed
#   fomod/      the installer (fomod/ in the repo; see cmake/StageFomod.cmake)
#   LICENSE.txt, THIRD_PARTY_NOTICES.txt  the GPL and the libraries' licences
# Optionally also copy straight into a mod folder by configuring with
# -DDEPLOY_DIR="<path to mod folder>".
# ------------------------------------------------------------------------------
# Per configuration: a Debug build (its DLL needs the debug runtime, which
# players don't have) gets "-debug" on its zip, so it can't pass for a release.
set(STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/package/$<CONFIG>")
set(DIST_ZIP "${PROJECT_SOURCE_DIR}/dist/${PROJECT_NAME}-${CHARSELECT_DISPLAY_VERSION}$<$<CONFIG:Debug>:-debug>.zip")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E remove_directory "${STAGE_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE_DIR}/Core/SKSE/Plugins" "${PROJECT_SOURCE_DIR}/dist"
	# Loose mod files (INIs, Interface\CharSelectMenu.swf) ship as-is, except
	# CharSelect.ini, which comes from the chosen location.
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/dist-files" "${STAGE_DIR}/Core"
	COMMAND ${CMAKE_COMMAND} -E rm -f "${STAGE_DIR}/Core/SKSE/Plugins/CharSelect.ini"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${STAGE_DIR}/Core/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.public.pdb" "${STAGE_DIR}/Core/SKSE/Plugins/${PROJECT_NAME}.pdb"
	# GPL: the licence and the bundled libraries' notices go with the binary
	# (in the archive root, which the installer doesn't copy into Data).
	COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/LICENSE" "${STAGE_DIR}/LICENSE.txt"
	COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" "${STAGE_DIR}/"
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DSTAGE_DIR=${STAGE_DIR}
		-DPROJECT_VERSION=${CHARSELECT_DISPLAY_VERSION} -DPROJECT_AUTHOR=${PROJECT_AUTHOR}
		-P "${PROJECT_SOURCE_DIR}/cmake/StageFomod.cmake"
	COMMENT "Staging ${PROJECT_NAME} package")
# Separate step: WORKING_DIRECTORY must already exist when the step starts.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E rm -f "${DIST_ZIP}"
	COMMAND ${CMAKE_COMMAND} -E tar cf "${DIST_ZIP}" --format=zip -- fomod Core Locations LICENSE.txt THIRD_PARTY_NOTICES.txt
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
