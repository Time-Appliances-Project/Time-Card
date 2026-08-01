file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.[ch])
file(GLOB COMMON_SOURCES ${PROJECT_SOURCE_DIR}/common/*.[ch])
add_executable(${PROJECT_NAME} ${SOURCES} ${COMMON_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
	${oscillator-disciplining_LIBRARIES}
	${gps_LIBRARIES}
	${ubloxcfg_LIBRARIES}
	pthread
	m
	json-c)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
