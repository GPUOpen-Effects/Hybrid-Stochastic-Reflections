project (HSRSample_DX12)

add_compile_options(/MP)

file(GLOB Sources_src 
	Sources/*.h
	Sources/*.cpp
	)

file(GLOB Shaders_src
		../Shaders/*.hlsl
		../Shaders/*.h
		../../../ffx-dnsr/ffx-reflection-dnsr/*.h
		../../../ffx-dnsr/ffx-reflection-dnsr/*.hlsl
		../../../ffx-sssr/ffx-sssr/*.h
		../../../ffx-sssr/ffx-sssr/*.hlsl
		../../../ffx-spd/ffx-spd/ffx_spd.h
		../../../ffx-fsr/ffx-fsr/*.h
		../../../ffx-fsr/ffx-fsr/*.hlsl
	)

file(GLOB Common_src
	../Common/config.json
)
    
source_group("Sources"            FILES ${Sources_src})    
source_group("Shaders"            FILES ${Shaders_src})    
source_group("Common"             FILES ${Common_src})    

set_source_files_properties(${Shaders_src} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Common_src} PROPERTIES VS_TOOL_OVERRIDE "Text")
    
function(copyCommand list dest)
	foreach(fullFileName ${list})    
		get_filename_component(file ${fullFileName} NAME)
		message("Generating custom command for ${fullFileName}")
		add_custom_command(
			OUTPUT   ${dest}/${file}
			PRE_BUILD
			COMMAND cmake -E make_directory ${dest}
			COMMAND cmake -E copy ${fullFileName}  "${dest}"
			MAIN_DEPENDENCY  ${fullFileName}
			COMMENT "Updating ${file} into ${dest}" 
		)
	endforeach()    
endfunction()

macro(copy_files GLOBPAT DESTINATION)
  file(GLOB COPY_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${GLOBPAT})
  add_custom_target(copy ALL COMMENT "Copying files: ${GLOBPAT}")
  foreach(FILENAME ${COPY_FILES})
    set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME}")
    set(DST "${DESTINATION}/${FILENAME}")
	string(REPLACE "../Shaders/" "" DST ${DST})
	get_filename_component( 
		DIRECTORY_PATH
		${DST}
		DIRECTORY
	)
	message("Generating custom command to copy ${SRC} to ${DST}")
	message("Dest folder ${DIRECTORY_PATH}")
    add_custom_command(
	  OUTPUT   ${DST}
	  PRE_BUILD
	  COMMAND ${CMAKE_COMMAND} -E make_directory ${DIRECTORY_PATH}
      COMMAND ${CMAKE_COMMAND} -E copy ${SRC} ${DST}
	  MAIN_DEPENDENCY  ${SRC}
    )
  endforeach(FILENAME)
endmacro(copy_files)

copyCommand("${Shaders_src}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibDX)
copyCommand("${Common_src}" ${CMAKE_HOME_DIRECTORY}/bin)

add_executable(${PROJECT_NAME} WIN32 ${Sources_src} ${Shaders_src} ${Common_src}) 
target_link_libraries (${PROJECT_NAME} LINK_PUBLIC Cauldron_DX12 ImGUI amd_ags DXC)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin")

IF (MSVC)
	IF (CMAKE_MAJOR_VERSION LESS 3)
		MESSAGE(WARNING "CMake version 3.0 or newer is required use build variable TARGET_FILE")
	ELSE()
		ADD_CUSTOM_COMMAND(
			TARGET ${PROJECT_NAME}
			POST_BUILD
			COMMAND "mt.exe" -manifest \"${CMAKE_CURRENT_SOURCE_DIR}\\dpiawarescaling.manifest\" -inputresource:\"$<TARGET_FILE:${PROJECT_NAME}>\"\;\#1 -outputresource:\"$<TARGET_FILE:${PROJECT_NAME}>\"\;\#1
			COMMENT "Adding display aware manifest..." 
		)
	ENDIF()
ENDIF(MSVC)