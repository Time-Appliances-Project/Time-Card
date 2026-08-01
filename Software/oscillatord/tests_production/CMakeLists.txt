if(BUILD_TESTS)
	file(GLOB EXTTS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/utils/extts.[ch])
	file(GLOB COMMON_SOURCES
		${PROJECT_SOURCE_DIR}/common/config.[ch]
		${PROJECT_SOURCE_DIR}/common/log.[ch]
		${PROJECT_SOURCE_DIR}/common/mro50_ioctl.h
		${PROJECT_SOURCE_DIR}/common/utils.[ch]
	)
	file(GLOB mro_test_prod_SOURCES
		${PROJECT_SOURCE_DIR}/common/f9_defvalsets.[ch]
		${PROJECT_SOURCE_DIR}/common/gnss-config.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/mro_test_prod.c
		${PROJECT_SOURCE_DIR}/src/ntpshm/ppsthread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timehint.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timespec.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timespec_str.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshm.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmwrite.[ch]
		${PROJECT_SOURCE_DIR}/src/phasemeter.[ch]
		${PROJECT_SOURCE_DIR}/src/gnss.[ch]
		${PROJECT_SOURCE_DIR}/src/oscillator.[ch]
		${PROJECT_SOURCE_DIR}/src/oscillator_factory.[ch]
		${PROJECT_SOURCE_DIR}/src/oscillators/mRo50_oscillator.c
	)
	file(GLOB gnss_config_prod_SOURCES
		${PROJECT_SOURCE_DIR}/common/f9_defvalsets.[ch]
		${PROJECT_SOURCE_DIR}/src/gnss.[ch]
		${PROJECT_SOURCE_DIR}/common/gnss-config.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/gnss_config_prod.c
		${PROJECT_SOURCE_DIR}/src/ntpshm/ppsthread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timehint.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timespec.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timespec_str.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshm.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmwrite.[ch]
	)
	file(GLOB gnss_test_prod_SOURCES
		${PROJECT_SOURCE_DIR}/common/f9_defvalsets.[ch]
		${PROJECT_SOURCE_DIR}/src/gnss.[ch]
		${PROJECT_SOURCE_DIR}/common/gnss-config.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/gnss_test_prod.c
		${PROJECT_SOURCE_DIR}/src/ntpshm/ppsthread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timehint.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timespec.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/timespec_str.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshm.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmread.[ch]
		${PROJECT_SOURCE_DIR}/src/ntpshm/ntpshmwrite.[ch]
	)
	file(GLOB io_test_prod_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/io_test_prod.c)
	file(GLOB phase_error_tracking_test_prod_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/phase_error_tracking_test_prod.c)
	file(GLOB ptp_test_prod_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ptp_test_prod.c)
	file(GLOB write_eeprom_prod_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/write_eeprom_prod.c)
	file(GLOB ocpdir_test_prod_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ocpdir_test_prod.c)

	pkg_check_modules(SYSTEMD REQUIRED libsystemd)
	include_directories(${SYSTEMD_INCLUDE_DIRS})

	add_executable(gnss_config_prod ${gnss_config_prod_SOURCES} ${COMMON_SOURCES})
	add_executable(gnss_test_prod ${gnss_test_prod_SOURCES} ${COMMON_SOURCES})
	add_executable(mro_test_prod ${mro_test_prod_SOURCES} ${COMMON_SOURCES})
	add_executable(io_test_prod ${io_test_prod_SOURCES} ${EXTTS_SOURCES} ${COMMON_SOURCES})
	add_executable(phase_error_tracking_test_prod ${phase_error_tracking_test_prod_SOURCES} ${COMMON_SOURCES})
	add_executable(ptp_test_prod ${ptp_test_prod_SOURCES} ${COMMON_SOURCES})
	add_executable(write_eeprom_prod ${write_eeprom_prod_SOURCES} ${COMMON_SOURCES})
	add_executable(ocpdir_test_prod ${ocpdir_test_prod_SOURCES} ${COMMON_SOURCES})

	target_link_libraries(gnss_config_prod PRIVATE m pthread ${ubloxcfg_LIBRARIES})
	target_link_libraries(gnss_test_prod PRIVATE m pthread ${ubloxcfg_LIBRARIES})
	target_link_libraries(mro_test_prod PRIVATE m pthread ${ubloxcfg_LIBRARIES})
	target_link_libraries(io_test_prod PRIVATE m)
	target_link_libraries(phase_error_tracking_test_prod PRIVATE m json-c ${SYSTEMD_LIBRARIES})
	target_link_libraries(ptp_test_prod PRIVATE m)
	target_link_libraries(write_eeprom_prod PRIVATE m)
	target_link_libraries(ocpdir_test_prod PRIVATE m)

	install(TARGETS gnss_config_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS gnss_test_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS mro_test_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS io_test_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS phase_error_tracking_test_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS ptp_test_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS write_eeprom_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS ocpdir_test_prod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

endif(BUILD_TESTS)
