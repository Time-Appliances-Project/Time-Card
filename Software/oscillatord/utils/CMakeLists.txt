if(BUILD_UTILS)
	file(GLOB ART_EEPROM_MANAGER_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/art_disciplining_manager.c
	)
	file(GLOB COMMON_SOURCES
		${PROJECT_SOURCE_DIR}/common/config.[ch]
		${PROJECT_SOURCE_DIR}/common/eeprom_config.[ch]
		${PROJECT_SOURCE_DIR}/common/log.[ch]
		${PROJECT_SOURCE_DIR}/common/mro50_ioctl.h
		${PROJECT_SOURCE_DIR}/common/utils.[ch]
	)
	file(GLOB EEPROM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/eeprom.[ch])
	file(GLOB EXTTS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extts.[ch])
	file(GLOB ART_EEPROM_FORMAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/art_eeprom_format.c)
	file(GLOB ART_EEPROM_REFORMAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/art_eeprom_reformat.c)
	file(GLOB ART_MONITORING_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/art_monitoring_client.c ${PROJECT_SOURCE_DIR}/src/monitoring.h)
	file(GLOB ART_TEMPERATURE_TABLE_MANAGER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/art_temperature_table_manager.c)
	file(GLOB ART_EEPROM_FILES_UPDATER ${CMAKE_CURRENT_SOURCE_DIR}/art_eeprom_files_updater.c)


	add_executable(art_disciplining_manager ${ART_EEPROM_MANAGER_SOURCES} ${COMMON_SOURCES})
	add_executable(art_eeprom_format ${ART_EEPROM_FORMAT_SOURCES} ${COMMON_SOURCES} ${EEPROM_SOURCES})
	add_executable(art_eeprom_reformat ${ART_EEPROM_REFORMAT_SOURCES} ${COMMON_SOURCES} ${EEPROM_SOURCES})
	add_executable(art_monitoring_client ${ART_MONITORING_SOURCES} ${COMMON_SOURCES})
	add_executable(art_temperature_table_manager ${ART_TEMPERATURE_TABLE_MANAGER_SOURCES} ${COMMON_SOURCES})
	add_executable(art_eeprom_files_updater ${ART_EEPROM_FILES_UPDATER} ${COMMON_SOURCES})

	target_link_libraries(art_disciplining_manager PRIVATE
		m)
	target_link_libraries(art_eeprom_format PRIVATE
		${oscillator-disciplining_LIBRARIES}
		m)
	target_link_libraries(art_eeprom_reformat PRIVATE
		${oscillator-disciplining_LIBRARIES}
		m)
	target_link_libraries(art_monitoring_client PRIVATE
		json-c
		m)
	target_link_libraries(art_temperature_table_manager PRIVATE
		m)
	target_link_libraries(art_eeprom_files_updater PRIVATE
		m)

	install(TARGETS art_disciplining_manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_eeprom_format RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_eeprom_reformat RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_monitoring_client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_temperature_table_manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_eeprom_files_updater RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

endif(BUILD_UTILS)
