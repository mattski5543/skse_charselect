# Writes the installer's files into a staged package (run with cmake -P after
# the plugin and dist-files are staged under STAGE_DIR/Core):
#   STAGE_DIR/fomod/info.xml, ModuleConfig.xml, and images/ if fomod/images exists
#   STAGE_DIR/Locations/<location>/<mode>/SKSE/Plugins/CharSelect.ini, one for
#   every line of fomod/locations.txt and fomod/mainmenu.txt together: the
#   shipped CharSelect.ini with that sLobbyCell, bReplaceMainMenu,
#   bReplaceAtStartup and bLobbyOnLoad.
# Arguments: -DSOURCE_DIR=... -DSTAGE_DIR=... -DPROJECT_VERSION=... -DPROJECT_AUTHOR=...

foreach(arg SOURCE_DIR STAGE_DIR PROJECT_VERSION PROJECT_AUTHOR)
	if(NOT DEFINED ${arg})
		message(FATAL_ERROR "StageFomod.cmake: ${arg} is not set")
	endif()
endforeach()

file(MAKE_DIRECTORY "${STAGE_DIR}/fomod")
configure_file("${SOURCE_DIR}/fomod/info.xml.in" "${STAGE_DIR}/fomod/info.xml" @ONLY)
file(COPY "${SOURCE_DIR}/fomod/ModuleConfig.xml" DESTINATION "${STAGE_DIR}/fomod")
# A screenshot per location, if there are any: the installer shows none without.
if(EXISTS "${SOURCE_DIR}/fomod/images")
	file(COPY "${SOURCE_DIR}/fomod/images" DESTINATION "${STAGE_DIR}/fomod")
endif()

file(READ "${SOURCE_DIR}/dist-files/SKSE/Plugins/CharSelect.ini" ini)
foreach(key sLobbyCell bReplaceMainMenu bReplaceAtStartup bLobbyOnLoad)
	string(REGEX MATCHALL "\n${key} = [^\r\n]*" found "${ini}")
	list(LENGTH found count)
	if(NOT count EQUAL 1)
		message(FATAL_ERROR "StageFomod.cmake: CharSelect.ini needs exactly one '${key} = ' line (found ${count})")
	endif()
endforeach()

set(modes "")
file(STRINGS "${SOURCE_DIR}/fomod/mainmenu.txt" mode_lines)
foreach(line IN LISTS mode_lines)
	if(line MATCHES "^#" OR line STREQUAL "")
		continue()
	endif()
	if(NOT line MATCHES "^[A-Za-z0-9]+=[01],[01],[01]$")
		message(FATAL_ERROR "StageFomod.cmake: bad line in mainmenu.txt: '${line}'")
	endif()
	list(APPEND modes "${line}")
endforeach()

file(STRINGS "${SOURCE_DIR}/fomod/locations.txt" lines)
foreach(line IN LISTS lines)
	if(line MATCHES "^#" OR line STREQUAL "")
		continue()
	endif()
	if(NOT line MATCHES "^([A-Za-z0-9]+)=([A-Za-z0-9_]+(,[A-Za-z0-9_]+)*)$")
		message(FATAL_ERROR "StageFomod.cmake: bad line in locations.txt: '${line}'")
	endif()
	set(folder "${CMAKE_MATCH_1}")
	set(cell "${CMAKE_MATCH_2}")
	string(REGEX REPLACE "\nsLobbyCell = [^\r\n]*" "\nsLobbyCell = ${cell}" located "${ini}")
	foreach(mode IN LISTS modes)
		string(REGEX MATCH "^([A-Za-z0-9]+)=([01]),([01]),([01])$" parsed "${mode}")
		set(mode_folder "${CMAKE_MATCH_1}")
		set(replace "${CMAKE_MATCH_2}")
		set(at_startup "${CMAKE_MATCH_3}")
		set(on_load "${CMAKE_MATCH_4}")
		string(REGEX REPLACE "\nbReplaceMainMenu = [^\r\n]*" "\nbReplaceMainMenu = ${replace}" out "${located}")
		string(REGEX REPLACE "\nbReplaceAtStartup = [^\r\n]*" "\nbReplaceAtStartup = ${at_startup}" out "${out}")
		string(REGEX REPLACE "\nbLobbyOnLoad = [^\r\n]*" "\nbLobbyOnLoad = ${on_load}" out "${out}")
		file(WRITE "${STAGE_DIR}/Locations/${folder}/${mode_folder}/SKSE/Plugins/CharSelect.ini" "${out}")
	endforeach()
endforeach()
