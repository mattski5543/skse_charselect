# Writes the installer's files into a staged package (run with cmake -P after
# the plugin and dist-files are staged under STAGE_DIR/Core):
#   STAGE_DIR/fomod/info.xml, ModuleConfig.xml
#   STAGE_DIR/Locations/<folder>/SKSE/Plugins/CharSelect.ini, one per line of
#   fomod/locations.txt, each the shipped CharSelect.ini with its sLobbyCell.
# Arguments: -DSOURCE_DIR=... -DSTAGE_DIR=... -DPROJECT_VERSION=... -DPROJECT_AUTHOR=...

foreach(arg SOURCE_DIR STAGE_DIR PROJECT_VERSION PROJECT_AUTHOR)
	if(NOT DEFINED ${arg})
		message(FATAL_ERROR "StageFomod.cmake: ${arg} is not set")
	endif()
endforeach()

file(MAKE_DIRECTORY "${STAGE_DIR}/fomod")
configure_file("${SOURCE_DIR}/fomod/info.xml.in" "${STAGE_DIR}/fomod/info.xml" @ONLY)
file(COPY "${SOURCE_DIR}/fomod/ModuleConfig.xml" DESTINATION "${STAGE_DIR}/fomod")

file(READ "${SOURCE_DIR}/dist-files/SKSE/Plugins/CharSelect.ini" ini)
string(REGEX MATCHALL "\nsLobbyCell = [^\r\n]*" found "${ini}")
list(LENGTH found count)
if(NOT count EQUAL 1)
	message(FATAL_ERROR "StageFomod.cmake: CharSelect.ini needs exactly one 'sLobbyCell = ' line (found ${count})")
endif()

file(STRINGS "${SOURCE_DIR}/fomod/locations.txt" lines)
foreach(line IN LISTS lines)
	if(line MATCHES "^#" OR line STREQUAL "")
		continue()
	endif()
	if(NOT line MATCHES "^([A-Za-z0-9]+)=([A-Za-z0-9_]+)$")
		message(FATAL_ERROR "StageFomod.cmake: bad line in locations.txt: '${line}'")
	endif()
	set(folder "${CMAKE_MATCH_1}")
	set(cell "${CMAKE_MATCH_2}")
	string(REGEX REPLACE "\nsLobbyCell = [^\r\n]*" "\nsLobbyCell = ${cell}" out "${ini}")
	file(WRITE "${STAGE_DIR}/Locations/${folder}/SKSE/Plugins/CharSelect.ini" "${out}")
endforeach()
