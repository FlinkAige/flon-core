macro( copy_bin file )
   add_custom_command( TARGET ${file} POST_BUILD COMMAND mkdir -p ${CMAKE_BINARY_DIR}/bin )
   add_custom_command( TARGET ${file} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${file}${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_BINARY_DIR}/bin/ )
endmacro( copy_bin )

macro( copy_bin_to from to )
   add_custom_command( TARGET ${from} POST_BUILD COMMAND mkdir -p ${CMAKE_BINARY_DIR}/bin )
   add_custom_command( TARGET ${from} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${from}${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_BINARY_DIR}/bin/${to} )
endmacro( copy_bin_to )
