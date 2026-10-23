cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME EPuck_STM32)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

file(GLOB QORVO_DRIVER_SOURCES
    ThirdParty/qorvo_dw3/driver/*.c
)

file(GLOB QORVO_DW3000_SOURCES
    ThirdParty/qorvo_dw3/driver/dw3000/*.c
)

file(GLOB QORVO_PLATFORM_SOURCES
    ThirdParty/qorvo_dw3/platform/*.c
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/dwm3000_port.c
    Core/Src/epuck_link.c
    Core/Src/esp_link.c
    Core/Src/usb_console.c
    Core/Src/uwb_link.c
    Core/Src/phy_interop.c
    Core/Src/uwb_diag_profile.c
    Core/Src/uwb_config.c
    Core/Src/uwb_config_flash.c
    Core/Src/uwb_runtime.c
    Core/Src/mul_hybrid_radio.c
    Core/Src/mul_ads_radio.c
    ThirdParty/mul_core/mul_hybrid.c
    ThirdParty/mul_core/mul_legacy_twr.c
    ThirdParty/mul_core/mul_clock.c
    ThirdParty/mul_core/mul_relation.c
    ThirdParty/mul_core/mul_observation.c
    ThirdParty/mul_core/mul_downlink.c
    ThirdParty/mul_core/mul_localization.c
    ThirdParty/mul_core/mul_telemetry.c
    ThirdParty/mul_core/mul_beacon.c
    ThirdParty/mul_core/mul_time.c
    ThirdParty/mul_core/mul_ads.c
    Core/Src/mul_diag.c
    Core/Src/mul_timing.c
    ThirdParty/mul_core/mul_core.c
    ThirdParty/mul_core/mul_common.c
    ${QORVO_DRIVER_SOURCES}
    ${QORVO_DW3000_SOURCES}
    ${QORVO_PLATFORM_SOURCES}
    ThirdParty/qorvo_dw3/driver/lib/qmath/src/qmath.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ThirdParty/qorvo_dw3/platform
    ThirdParty/mul_core
    ThirdParty/qorvo_dw3/driver
    ThirdParty/qorvo_dw3/driver/dw3000
    ThirdParty/qorvo_dw3/driver/lib/qmath/include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Enable float support in printf
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
# Experimental PHY packet diagnostic only; app_config/NVM/defaults stay intact.
set(PHY_INTEROP_ROLE "0" CACHE STRING "0=off, 1=TX, 2=RX")
if(NOT PHY_INTEROP_ROLE MATCHES "^[012]$")
    message(FATAL_ERROR "PHY_INTEROP_ROLE must be 0, 1 or 2")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PHY_INTEROP_ROLE=${PHY_INTEROP_ROLE})

# Offline, pinned canonical source; fail on drift at configure AND every build.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/ThirdParty/mul_core/verify.py
    RESULT_VARIABLE MUL_CORE_VERIFY_RESULT)
if(NOT MUL_CORE_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "MUL core snapshot verification failed")
endif()
add_custom_target(verify_mul_core ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/ThirdParty/mul_core/verify.py)
add_dependencies(${CMAKE_PROJECT_NAME} verify_mul_core)
set(MUL_DIAG_ROLE "0" CACHE STRING "Stage3G: 0=off, 1=TX, 2=RX")
set(MUL_TWR_ROLE "0" CACHE STRING "ADS: 0=off, 1=initiator, 2=responder")
option(MUL_UNIVERSAL "Runtime configurable UWB-only ADS image (both roles)" OFF)
if(MUL_UNIVERSAL)
    if(NOT MUL_TWR_ROLE STREQUAL "0" OR NOT MUL_DIAG_ROLE STREQUAL "0" OR NOT PHY_INTEROP_ROLE STREQUAL "0")
        message(FATAL_ERROR "Universal profile requires legacy role selectors all zero")
    endif()
    # Pinned core uses this macro only as a profile inclusion/timing guard.
    # In the universal adapter it never selects the runtime ADS role.
    set(MUL_TWR_ROLE "2")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MUL_UNIVERSAL=$<BOOL:${MUL_UNIVERSAL}>)
set(MUL_TWR_SESSION "0" CACHE STRING "Fresh nonzero uint64 decimal initiator run/session ID")
option(MUL_TIMING_TRACE "Opt-in MUL diagnostic/ADS DWT cycle timing" OFF)
if(NOT MUL_DIAG_ROLE MATCHES "^[012]$")
    message(FATAL_ERROR "MUL_DIAG_ROLE must be 0, 1 or 2")
endif()
if(NOT MUL_DIAG_ROLE STREQUAL "0" AND NOT PHY_INTEROP_ROLE STREQUAL "0")
    message(FATAL_ERROR "Stage3F and Stage3G diagnostics are mutually exclusive")
endif()
if(MUL_TIMING_TRACE AND MUL_DIAG_ROLE STREQUAL "0" AND MUL_TWR_ROLE STREQUAL "0")
    message(FATAL_ERROR "Timing instrumentation requires a MUL diagnostic or ADS profile")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    MUL_DIAG_ROLE=${MUL_DIAG_ROLE} MUL_TIMING_TRACE=$<BOOL:${MUL_TIMING_TRACE}>)

# Secondary-function isolation only: keep dsPIC UART safety/STOP servicing.
option(MUL_UWB_ONLY "Disable ESP and automatic sensor streaming; prohibit ARM/movement" OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MUL_UWB_ONLY=$<BOOL:${MUL_UWB_ONLY}>)

if(NOT MUL_TWR_ROLE MATCHES "^[012]$" OR NOT MUL_TWR_SESSION MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Invalid ADS role/session")
endif()
if(NOT MUL_TWR_ROLE STREQUAL "0")
    if(NOT MUL_UWB_ONLY OR NOT MUL_DIAG_ROLE STREQUAL "0" OR NOT PHY_INTEROP_ROLE STREQUAL "0")
        message(FATAL_ERROR "ADS requires UWB-only and exclusive radio ownership")
    endif()
    if(MUL_TWR_ROLE STREQUAL "1" AND MUL_TWR_SESSION STREQUAL "0")
        message(FATAL_ERROR "Initiator needs a fresh nonzero session ID")
    endif()
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    MUL_TWR_ROLE=${MUL_TWR_ROLE} MUL_TWR_SESSION=${MUL_TWR_SESSION}ULL)

option(MUL_ADS_TRACE_TIMES "Emit session-tagged six raw timestamps for ADS audit" ON)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MUL_ADS_TRACE_TIMES=$<BOOL:${MUL_ADS_TRACE_TIMES}>)

# Legacy TDMA layout must match both ALRs; this never changes their scheduler.
set(MUL_HYBRID_SLOT_MS "10" CACHE STRING "Legacy API fallback only; operational spacing uses persistent slot_ms")
if(NOT MUL_HYBRID_SLOT_MS MATCHES "^(500|50|10)$")
 message(FATAL_ERROR "MUL_HYBRID_SLOT_MS must be 500, 50, or 10")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MUL_FOLLOWER_SLOT_MS=${MUL_HYBRID_SLOT_MS} MUL_PROP_MAX_AGE_MS=6000)

# Raw per-packet frequency investigation only; never enables a rate estimator.
option(MUL_HYBRID_FREQ_DIAG "Capture raw hybrid carrier/CIA offset before RX rearm" OFF)
if(MUL_HYBRID_FREQ_DIAG AND NOT MUL_UNIVERSAL)
 message(FATAL_ERROR "MUL_HYBRID_FREQ_DIAG requires MUL_UNIVERSAL")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MUL_HYBRID_FREQ_DIAG=$<BOOL:${MUL_HYBRID_FREQ_DIAG}>)
