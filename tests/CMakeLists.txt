# ======================================================================
#               test_libinput
# ======================================================================
set(DMOD_MODULE_NAME        test_libinput)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         8192)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/test_libinput.dmr)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    libinput_test.c
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.5
    dmosi
)

target_link_libraries(${DMOD_MODULE_NAME}
    libinput_if
)

# The cfg directory of the tests (DMINPUT_CFG_DIR): the files libinput's package brings (keymaps/libinput, layouts/libinput) with a
# layout of a small keyboard (compact), and the layouts of a driver (layouts/mydrv)
set(LIBINPUT_TEST_CFG "${CMAKE_CURRENT_BINARY_DIR}/cfg")
file(GLOB _keymaps CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/configs/keymaps/*.ini")
file(GLOB _layouts CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/configs/layouts/*.ini" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cfg/layouts/libinput/*.ini")
file(GLOB _driver_layouts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cfg/layouts/mydrv/*.ini")
file(COPY ${_keymaps} DESTINATION "${LIBINPUT_TEST_CFG}/keymaps/libinput")
file(COPY ${_layouts} DESTINATION "${LIBINPUT_TEST_CFG}/layouts/libinput")
file(COPY ${_driver_layouts} DESTINATION "${LIBINPUT_TEST_CFG}/layouts/mydrv")

target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE
    LIBINPUT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    LIBINPUT_KEYMAPS_DIR="${PROJECT_SOURCE_DIR}/configs/keymaps"
    LIBINPUT_TEST_CFG="${LIBINPUT_TEST_CFG}"
)

find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND cmake -E env DMOD_DMF_DIR=${DMOD_DMF_DIR} bash -c
            "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}-local.dmd -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf"
    )
else()
    message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${DMOD_MODULE_NAME}. Build the tools first with -DDMOD_BUILD_TOOLS=ON")
endif()
