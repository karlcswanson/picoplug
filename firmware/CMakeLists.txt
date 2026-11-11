# picolldp firmware
#
#   cmake -S firmware -B build-pico2                     # W5500-EVB-Pico2 (RP2350, default)
#   cmake -S firmware -B build-pico -DPICO_BOARD=wiznet_w5500_evb_pico   # RP2040
#   cmake -S firmware -B build-w55rp20 -DPICO_BOARD=wiznet_w55rp20_evb_pico # W55RP20 (RP2040+W5500 SiP)
#   cmake --build build-pico2
#
cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk CACHE PATH "Pico SDK (submodule)")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards CACHE PATH "Custom board headers")
if(NOT PICO_BOARD)
    set(PICO_BOARD wiznet_w5500_evb_pico2 CACHE STRING "Target board")
endif()

# The SDK needs a picotool matching its own version (2.3.x). Rather than
# depend on whatever is installed, fetch and build it once into lib/.picotool
# (gitignored) and share it between all build directories.
if(NOT DEFINED PICOTOOL_FORCE_FETCH_FROM_GIT)
    set(PICOTOOL_FORCE_FETCH_FROM_GIT ON CACHE BOOL "Build the SDK's matching picotool")
endif()
if(NOT DEFINED PICOTOOL_FETCH_FROM_GIT_PATH)
    set(PICOTOOL_FETCH_FROM_GIT_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/.picotool CACHE PATH "Shared picotool build")
endif()

include(lib/pico-sdk/external/pico_sdk_import.cmake)

project(picolldp C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(USB_NET_ECM "Use CDC-ECM instead of CDC-NCM for the USB network link" OFF)
option(USB_DEBUG_CDC "Add a CDC-ACM serial port carrying the firmware log" ON)
option(USB_MDNS "Answer picolldp.local (mDNS) on the USB link" ON)
option(USB_TRACE "Capture TinyUSB's debug log (level 2) in a RAM buffer read over SWD; diagnostics only" OFF)

set(IOLIB ${CMAKE_CURRENT_LIST_DIR}/lib/ioLibrary_Driver/Ethernet)

# The viewer page is the Svelte app in web/, built by npm into one file, web/dist/index.html.
find_program(NPM npm REQUIRED)
set(WEB_DIR ${CMAKE_CURRENT_LIST_DIR}/web)
if(NOT EXISTS ${WEB_DIR}/node_modules)
    execute_process(COMMAND ${NPM} ci WORKING_DIRECTORY ${WEB_DIR} COMMAND_ERROR_IS_FATAL ANY)
endif()
file(GLOB_RECURSE WEB_SRC CONFIGURE_DEPENDS ${WEB_DIR}/src/*)
set(WEB_INDEX ${WEB_DIR}/dist/index.html)
add_custom_command(
    OUTPUT ${WEB_INDEX}
    COMMAND ${NPM} run build
    WORKING_DIRECTORY ${WEB_DIR}
    DEPENDS ${WEB_SRC} ${WEB_DIR}/index.html ${WEB_DIR}/vite.config.js ${WEB_DIR}/package-lock.json
    COMMENT "Building the viewer page")

# Embed the viewer page, its icon and the web-app manifest as C arrays.
set(EMBED_HDRS)
foreach(_f index.html:index_html icon.png:icon_png manifest.json:manifest_json)
    string(REPLACE ":" ";" _p ${_f})
    list(GET _p 0 _file)
    list(GET _p 1 _sym)
    set(_in ${CMAKE_CURRENT_LIST_DIR}/src/web/${_file})
    if(_file STREQUAL "index.html")
        set(_in ${WEB_INDEX})
    endif()
    set(_hdr ${CMAKE_CURRENT_BINARY_DIR}/generated/${_sym}.h)
    add_custom_command(
        OUTPUT ${_hdr}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND ${CMAKE_COMMAND} -DIN=${_in}
                -DOUT=${_hdr} -DNAME=${_sym}
                -P ${CMAKE_CURRENT_LIST_DIR}/tools/bin2c.cmake
        DEPENDS ${_in} tools/bin2c.cmake
        COMMENT "Embedding ${_in}")
    list(APPEND EMBED_HDRS ${_hdr})
endforeach()
set(INDEX_HTML_H ${EMBED_HDRS})

add_executable(picolldp
    ${INDEX_HTML_H}
    src/main.c
    src/json.c
    src/status.c
    src/api_server.c
    src/ws.c
    src/http_server.c
    src/lldp.c
    src/cdp.c
    src/lldp_tx.c
    src/cdp_tx.c
    src/advertise.c
    src/stp.c
    src/dhcp_info.c
    src/settings.c
    src/mac_id.c
    src/query.c
    src/settings_store.c
    src/tool.c
    src/blink.c
    src/eapol.c
    src/lacp.c
    src/peer.c
    src/path.c
    src/mcast.c
    src/lldp_print.c
    src/neighbor_table.c
    src/w5500_raw.c
    src/eth_net.c
    src/probe.c
    src/usb_stdio.c
    src/fault.c
    src/pio_spi.c
    src/usb_net.c
    src/usb_descriptors.c
    src/dhcpd.c
    ${IOLIB}/wizchip_conf.c
    ${IOLIB}/W5500/w5500.c
    # Ethernet framing lives in pico_lwip_netif along with SLIP/6LoWPAN/bridge;
    # pull in just the one file we need.
    ${PICO_LWIP_PATH}/src/netif/ethernet.c
)

target_include_directories(picolldp PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${IOLIB}
)

# PIO SPI program (used by the W55RP20; compiled for every board, harmless)
pico_generate_pio_header(picolldp ${CMAKE_CURRENT_LIST_DIR}/src/pio_spi.pio)

if(USB_NET_ECM)
    # TinyUSB's ECM driver shares a source file with RNDIS and needs the
    # RNDIS protocol header and report handler from lib/networking.
    target_sources(picolldp PRIVATE ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c)
    target_include_directories(picolldp PRIVATE ${PICO_TINYUSB_PATH}/lib/networking)
    set_source_files_properties(${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(USB_TRACE)
    target_sources(picolldp PRIVATE src/usb_trace.c)
    # The SDK passes CFG_TUSB_DEBUG=0 on the command line; tusb_config.h raises it when USB_TRACE is set.
    target_compile_definitions(picolldp PRIVATE USB_TRACE=1 CFG_TUSB_DEBUG_PRINTF=usb_trace_printf)
endif()

# Use our patched copy of TinyUSB's NCM driver (see src/tusb_ncm_device.c)
# instead of the SDK's: drop the SDK file from the interface library's sources.
get_target_property(_tusb_srcs tinyusb_device_base INTERFACE_SOURCES)
list(FILTER _tusb_srcs EXCLUDE REGEX "class/net/ncm_device\\.c$")
set_property(TARGET tinyusb_device_base PROPERTY INTERFACE_SOURCES ${_tusb_srcs})
target_sources(picolldp PRIVATE src/tusb_ncm_device.c)
target_include_directories(picolldp PRIVATE ${PICO_TINYUSB_PATH}/src/class/net)   # ncm.h, as the original file includes it

target_compile_definitions(picolldp PRIVATE
    _WIZCHIP_=5500
    PICO_BOARD="${PICO_BOARD}"
    USB_NET_USE_ECM=$<BOOL:${USB_NET_ECM}>
    USB_DEBUG_CDC=$<BOOL:${USB_DEBUG_CDC}>
    USB_MDNS=$<BOOL:${USB_MDNS}>
    # The SDK default main stack is 2 KB; lwIP's mDNS/DHCP/DNS paths need more.
    PICO_STACK_SIZE=0x4000
    # Route panic() (and therefore lwIP asserts) to fault.c so the message survives.
    PICO_PANIC_FUNCTION=picolldp_panic
)

target_compile_options(picolldp PRIVATE -Wall -Wextra -Wno-unused-parameter)
# ioLibrary is not warning-clean; keep our own code strict, quiet theirs.
set_source_files_properties(${IOLIB}/wizchip_conf.c ${IOLIB}/W5500/w5500.c
    PROPERTIES COMPILE_OPTIONS "-w")

target_link_libraries(picolldp PRIVATE
    pico_stdlib
    pico_unique_id
    pico_bootrom
    hardware_spi
    hardware_pio
    hardware_watchdog
    hardware_flash
    hardware_sync
    tinyusb_device
    pico_lwip_core
    pico_lwip_core4
    pico_lwip_mdns
    pico_lwip_arch
)

# Debug output on UART0 (GPIO0/1). USB is reserved for the network gadget.
pico_enable_stdio_uart(picolldp 1)
pico_enable_stdio_usb(picolldp 0)

# Work around CMake 3.31.1 mangling the SDK's empty "LINKER:$<...>" option
# (the following -L search path then reaches ld untranslated and the
# linker script cannot find section_platform_end.incl). Giving the override
# path list a harmless non-empty value keeps every option well formed.
set_target_properties(picolldp PROPERTIES
    PICO_TARGET_LINKER_SCRIPT_OVERRIDE_PATHS ${CMAKE_CURRENT_LIST_DIR}/boards)

# Main stack: the SDK puts core 0's stack in the 4 KB SCRATCH_Y bank, too
# small for lwIP's mDNS/DHCP/DNS call chains. Relocate "SCRATCH_Y" to the
# top 16 KB of main SRAM (and shrink RAM accordingly) so PICO_STACK_SIZE
# above fits. Scratch Y is otherwise unused by this firmware.
if(PICO_PLATFORM MATCHES "rp2350")
    set(_stack_vars "RAM_LENGTH=496k;SCRATCH_Y_ORIGIN=0x2007C000;SCRATCH_Y_LENGTH=16k")
else()
    set(_stack_vars "RAM_LENGTH=240k;SCRATCH_Y_ORIGIN=0x2003C000;SCRATCH_Y_LENGTH=16k")
endif()
set_target_properties(picolldp PROPERTIES PICO_TARGET_LINKER_SCRIPT_VARS "${_stack_vars}")

pico_add_extra_outputs(picolldp)
