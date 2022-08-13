add_executable(MediaServer main.cpp)
target_compile_definitions(MediaServer
  PRIVATE ${COMPILE_DEFINITIONS})
target_compile_options(MediaServer
  PRIVATE ${COMPILE_OPTIONS_DEFAULT})

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  target_link_libraries(MediaServer -Wl,--start-group ${MK_LINK_LIBRARIES} -Wl,--end-group)
else()
  target_link_libraries(MediaServer ${MK_LINK_LIBRARIES})
endif()

install(TARGETS MediaServer DESTINATION ${INSTALL_PATH_RUNTIME})