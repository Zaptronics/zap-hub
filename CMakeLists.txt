# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

# --- Web asset embedding (Windows + Linux) -----------------------------------

set(ASSETS_DIR     ${CMAKE_CURRENT_LIST_DIR}/http)
set(GENERATED_DIR  ${CMAKE_BINARY_DIR}/generated_http)
file(MAKE_DIRECTORY ${GENERATED_DIR})

# Prefer Python (cross-platform)
find_package(Python3 QUIET COMPONENTS Interpreter)

# Optional: allow fallback to xxd if Python isn't available
find_program(XXD_EXE xxd)

function(embed_asset SRC)
    if(NOT EXISTS "${SRC}")
        message(FATAL_ERROR "Asset file not found: ${SRC}")
    endif()

    get_filename_component(NAME "${SRC}" NAME)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" SYMBOL "${NAME}")
    set(OUT "${GENERATED_DIR}/${SYMBOL}.c")

    if(Python3_Interpreter_FOUND)
        add_custom_command(
            OUTPUT "${OUT}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/xxd_like.py"
                    "${SRC}" "${OUT}" "${SYMBOL}"
            DEPENDS "${SRC}" "${CMAKE_CURRENT_LIST_DIR}/tools/xxd_like.py"
            COMMENT "Embedding ${SRC} (python) -> ${OUT}"
            VERBATIM
        )
    elseif(XXD_EXE)
        # NOTE: avoid '>' redirection; xxd has an -o option in some variants, but not all.
        # If your xxd doesn't support writing directly, consider removing this fallback.
        add_custom_command(
            OUTPUT "${OUT}"
            COMMAND "${XXD_EXE}" -i -n "${SYMBOL}" "${SRC}"
            # Write stdout to file via CMake -E (portable) by using a small wrapper:
            # But CMake can't redirect, so this fallback is best avoided unless you wrap it.
            COMMENT "xxd fallback selected, but redirection is not portable. Use Python instead."
            VERBATIM
        )
        message(FATAL_ERROR "Python3 not found; xxd fallback is not reliably portable with Ninja. Install Python3 or remove fallback.")
    else()
        message(FATAL_ERROR "Neither Python3 nor xxd found. Install Python3 (recommended).")
    endif()

    set(GENERATED_ASSETS ${GENERATED_ASSETS} "${OUT}" PARENT_SCOPE)
endfunction()

# Add assets here
embed_asset("${ASSETS_DIR}/index.html")
# embed_asset("${ASSETS_DIR}/app.js")
# embed_asset("${ASSETS_DIR}/style.css")

# Target that generates all web assets
add_custom_target(generate_web_assets DEPENDS ${GENERATED_ASSETS})



# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Hub C CXX ASM)

set(HUB_FIRMWARE_VERSION "0.2.18")
set(HUB_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${HUB_GENERATED_DIR})
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/firmware_version.h.in
    ${HUB_GENERATED_DIR}/firmware_version.h
    @ONLY
)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Firmware version is defined once above and exported to code.

add_executable(Hub
    Hub.c
    cooling.c
    pumping.c
    sensing.c
    sweeping.c
    ztmb/log.c
    ztmb/bus.c
    ztmb/io_exp.c
    ztmb/cs_mux.c
    ztmb/adc.c
    drivers/ad5282.c
    drivers/ad7124.c
    drivers/thermocouple.c
    drivers/l6470.c
    drivers/tla2518.c
    drivers/emc2303.c
    drivers/sx1509.c
    drivers/pcal6416.c
    drivers/pac195x.c
    drivers/mpc4901.c
    modules/ztm_adc.c
    modules/ztm_psu_hv.c
    modules/ztm_pwm.c
    modules/ztm_psu_tec.c
    modules/ztm_stepper.c
    ztmb/cli_wifi.c
    ztmb/cli_cmds.c
    ztmb/http_server.c
    ztmb/web_assets.c
    ztmb/modules.c
    ztmb/ztmb.c
    ${GENERATED_ASSETS}
)

pico_set_program_name(Hub "Hub")
pico_set_program_version(Hub "${HUB_FIRMWARE_VERSION}")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Hub 1)
pico_enable_stdio_usb(Hub 0)


# Add the standard library to the build
target_link_libraries(Hub
        pico_stdlib
        pico_cyw43_arch_lwip_threadsafe_background
)

# Add the standard include files to the build
target_include_directories(Hub PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${HUB_GENERATED_DIR}
)

# For sys_thread_new / netconn API you also need:
target_link_libraries(Hub
    pico_lwip
    pico_lwip_netif
)

# Add any user requested libraries
target_link_libraries(Hub
        hardware_spi
        hardware_i2c

        )

pico_add_extra_outputs(Hub)

target_compile_definitions(Hub PRIVATE
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=16
    PICO_DEFAULT_UART_RX_PIN=17
    PICO_STDIO_UART_TX_BUFFER_SIZE=4096

    PICO_CYW43_ARCH_DEBUG_ENABLED=0
    CYW43_ENABLE_DEBUG=0
    #CYW43_DEBUG=0
    #CYW43_VERBOSE_DEBUG=0
    LWIP_DEBUG=0
)

#target_compile_options(Hub PRIVATE
#    -Wall
#    -Wextra
#    -Wpedantic
#    -Wno-unused-parameter
#)
