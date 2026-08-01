if(BUILD_TESTS)
	file(GLOB SIM_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/oscillator_sim.c
		${CMAKE_CURRENT_SOURCE_DIR}/ptspair.c
		${CMAKE_CURRENT_SOURCE_DIR}/ptspair.h
	)
	file(GLOB COMMON_SOURCES
		${PROJECT_SOURCE_DIR}/common/config.[ch]
		${PROJECT_SOURCE_DIR}/common/log.[ch]
		${PROJECT_SOURCE_DIR}/common/mro50_ioctl.h
		${PROJECT_SOURCE_DIR}/common/utils.[ch]
	)
	file(GLOB COMMON_GNSS_SOURCES
		${PROJECT_SOURCE_DIR}/common/f9_defvalsets.[ch]
		${PROJECT_SOURCE_DIR}/common/gnss-config.[ch]
	)
	file(GLOB MRO50_CTRL_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/mro50_ctrl.c
		${CMAKE_CURRENT_SOURCE_DIR}/mRo50.[ch]
	)
	file(GLOB ART_INTEGRATION_TEST_SUITE_SOURCES
		${PROJECT_SOURCE_DIR}/src/oscillator.[ch]
		${PROJECT_SOURCE_DIR}/src/oscillator_factory.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/art_integration_testsuite/*.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/mRo50.[ch]
	)
	file(GLOB ART_INTEGRATION_IN_SERVER_TEST_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/art_integration_in_server_test.c
		${CMAKE_CURRENT_SOURCE_DIR}/art_integration_testsuite/gnss_serial_test.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/art_integration_testsuite/mro_device_test.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/art_integration_testsuite/phase_error_tracking_test.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/art_integration_testsuite/ptp_device_test.[ch]
		${CMAKE_CURRENT_SOURCE_DIR}/mRo50.[ch]
	)
	file(GLOB EXTTS_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extts_test.c)
	file(GLOB EXTTS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extts.[ch])
	file(GLOB CONFIG_DISCOVERY_TEST_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/config_discovery_test.c)


	pkg_check_modules(SYSTEMD REQUIRED libsystemd)
	include_directories(${SYSTEMD_INCLUDE_DIRS})

	add_executable(oscillator_sim ${SIM_SOURCES} ${COMMON_SOURCES})
	add_executable(mro50_ctrl ${MRO50_CTRL_SOURCES} ${COMMON_SOURCES})
	add_executable(art_integration_test_suite
		${COMMON_SOURCES}
		${ART_INTEGRATION_TEST_SUITE_SOURCES}
		${EXTTS_SOURCES}
		${COMMON_GNSS_SOURCES}
	)
	add_executable(art_integration_in_server_test
		${ART_INTEGRATION_IN_SERVER_TEST_SOURCES}
		${COMMON_SOURCES}
		${EXTTS_SOURCES}
		${COMMON_GNSS_SOURCES}
	)
	add_executable(extts_test ${EXTTS_TEST_SOURCES} ${COMMON_SOURCES} ${EXTTS_SOURCES})
	add_executable(config_discovery_test ${CONFIG_DISCOVERY_TEST_SOURCES} ${COMMON_SOURCES})

	target_link_libraries(oscillator_sim PRIVATE m)
	target_link_libraries(mro50_ctrl PRIVATE m)
	target_link_libraries(art_integration_test_suite PRIVATE
		m
		json-c
		${ubloxcfg_LIBRARIES}
		${SYSTEMD_LIBRARIES})
	target_link_libraries(art_integration_in_server_test PRIVATE
		m
		json-c
		${ubloxcfg_LIBRARIES}
		${SYSTEMD_LIBRARIES})
	target_link_libraries(extts_test PRIVATE
		m)
	target_link_libraries(config_discovery_test PRIVATE m)

	add_test(NAME config_discovery COMMAND config_discovery_test)

	install(TARGETS oscillator_sim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS mro50_ctrl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_integration_test_suite RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS art_integration_in_server_test RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	add_subdirectory(lib_osc_sim_stubs)
endif(BUILD_TESTS)
