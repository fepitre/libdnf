add_executable(test_libdnf_main dnf-self-test.c)

configure_file(${CMAKE_SOURCE_DIR}/data/tests/modules/yum.repos.d/test.repo.in ${CMAKE_SOURCE_DIR}/data/tests/modules/yum.repos.d/test.repo @ONLY)

target_link_libraries(test_libdnf_main
    libdnf
    ${REPO_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GLIB_GOBJECT_LIBRARIES}
    ${GLIB_GIO_LIBRARIES}
    ${GLIB_GIO_UNIX_LIBRARIES}
    ${SOLV_LIBRARY}
    ${SOLVEXT_LIBRARY}
    ${RPMDB_LIBRARY}
)

add_test(test_libdnf_main test_libdnf_main)
set_property(TEST test_libdnf_main PROPERTY ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/libdnf")
