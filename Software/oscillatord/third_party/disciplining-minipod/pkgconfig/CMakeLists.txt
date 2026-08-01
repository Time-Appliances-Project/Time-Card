configure_file(pkgconfig.pc.in ${CMAKE_CURRENT_BINARY_DIR}/pkgconfig.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pkgconfig.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    RENAME "${PACKAGE_NAME}.pc")
