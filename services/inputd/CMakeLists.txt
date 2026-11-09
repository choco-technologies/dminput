# ======================================================================
#               inputd - the service of one input device
# ======================================================================
# One instance per input device, started by the device rule in
# configs/inputd.rules from configs/inputd@.ini.
set(DMOD_MODULE_NAME        inputd)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           2)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/inputd.dmr)
set(DMOD_MAL_IMPLS          )

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
)

# libsystemd: graceful stop. The rest is libinput's (libinput_if brings its headers).
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.5
    dmosi
    libsystemd
)

target_link_libraries(${DMOD_MODULE_NAME}
    libinput_if
)
