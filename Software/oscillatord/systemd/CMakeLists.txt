set(SYSTEMD_UNIT_DIR "${CMAKE_INSTALL_LIBDIR}/systemd/system" CACHE PATH
    "Installation directory for systemd unit files")

configure_file(oscillatord.service.in ${CMAKE_CURRENT_BINARY_DIR}/oscillatord.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/oscillatord.service
        DESTINATION ${SYSTEMD_UNIT_DIR})
configure_file(oscillatord@.service.in ${CMAKE_CURRENT_BINARY_DIR}/oscillatord@.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/oscillatord@.service
        DESTINATION ${SYSTEMD_UNIT_DIR})
