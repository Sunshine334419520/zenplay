cmake_minimum_required(VERSION 3.7)

macro(INSTALL_NUGET target id version)
	if (CMAKE_GENERATOR MATCHES "Visual Studio.*")
		unset(nuget_cmd)
		list(APPEND nuget_cmd install ${id} -Prerelease -Version ${version} -OutputDirectory ${CMAKE_BINARY_DIR}/packages)
		message("excute nuget install:${nuget_cmd}")
		execute_process(COMMAND nuget ${nuget_cmd} RESULT_VARIABLE NUGET_RESULT ENCODING AUTO)
		
		if (NUGET_RESULT EQUAL 0)
			target_link_libraries(${target} ${CMAKE_BINARY_DIR}/packages/${id}.${version}/build/native/${id}.targets)
		else()
			message(FATAL_ERROR "nuget excute fail")
		endif()
	else()
		message(FATAL_ERROR "INSTALL_NUGET noly use in Visual Studio")
	endif()
	
endmacro()
