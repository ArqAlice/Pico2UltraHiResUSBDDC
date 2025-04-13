# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

# Compiler Optimization
add_compile_options(-O2 -g -Wall)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# set boardtype
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
include(example_auto_set_url.cmake)

project(Pico2UltraHiResUSBDDC C CXX ASM)

# Use bundled pico‑extras from the repository
add_subdirectory(pico-extras)
set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-extras)
include(pico_extras_import.cmake)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add Source Directory
add_subdirectory(src)

# Compile Definitions
target_compile_definitions(Pico2UltraHiResUSBDDC PRIVATE
    AUDIO_FREQ_MAX=192000

    # ours are zero based, so say so
    PICO_USBDEV_USE_ZERO_BASED_INTERFACES=1

    # need large descriptor
    PICO_USBDEV_MAX_DESCRIPTOR_SIZE=256


    PICO_USBDEV_ISOCHRONOUS_BUFFER_STRIDE_TYPE=3
    PICO_USBDEV_ENABLE_DEBUG_TRACE
)