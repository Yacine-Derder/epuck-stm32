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
option(MUL_TIMING_TRACE "Opt-in Stage3G DWT cycle timing" OFF)
if(NOT MUL_DIAG_ROLE MATCHES "^[012]$")
    message(FATAL_ERROR "MUL_DIAG_ROLE must be 0, 1 or 2")
endif()
if(NOT MUL_DIAG_ROLE STREQUAL "0" AND NOT PHY_INTEROP_ROLE STREQUAL "0")
    message(FATAL_ERROR "Stage3F and Stage3G diagnostics are mutually exclusive")
endif()
if(MUL_TIMING_TRACE AND MUL_DIAG_ROLE STREQUAL "0")
    message(FATAL_ERROR "Timing instrumentation requires the Stage3G diagnostic")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    MUL_DIAG_ROLE=${MUL_DIAG_ROLE} MUL_TIMING_TRACE=$<BOOL:${MUL_TIMING_TRACE}>)

# Secondary-function isolation only: keep dsPIC UART safety/STOP servicing.
option(MUL_UWB_ONLY "Disable ESP and automatic sensor streaming; prohibit ARM/movement" OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MUL_UWB_ONLY=$<BOOL:${MUL_UWB_ONLY}>)
