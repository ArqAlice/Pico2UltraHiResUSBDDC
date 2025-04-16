# CMSIS-DSP 必要ソースを選択（使う関数に応じて最小限にする）
file(GLOB DSP_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_fir_interpolate_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/FilteringFunctions/arm_biquad_cascade_df1_init_f32.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/SupportFunctions/arm_q31_to_float.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../CMSIS/DSP/Source/BasicMathFunctions/arm_scale_f32.c
        )

# Add executable. Default name is the project name, version 0.1
add_executable(Pico2UltraHiResUSBDDC
        main.c
        general_func.c
        usb_device_control.c
        core1.c
        i2s_pio_interface.c
        transmit_to_dac.c
        upsampling_coef.c
        upsampling.c
        ringbuffer.c
        debug_with_gpio.c
        ess_specific.c
        ${DSP_SRC}
)

pico_set_program_name(Pico2UltraHiResUSBDDC "Pico2UltraHiResUSBDDC")
pico_set_program_version(Pico2UltraHiResUSBDDC "0.1")

# Generate PIO header
pico_generate_pio_header(Pico2UltraHiResUSBDDC ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

# Modify the below lines to enable/disable output over UART/USB
#pico_enable_stdio_uart(Pico2UltraHiResUSBDDC 0)
#pico_enable_stdio_usb(Pico2UltraHiResUSBDDC 0)

# Add the standard library to the build
target_link_libraries(Pico2UltraHiResUSBDDC
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(Pico2UltraHiResUSBDDC PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../pico-extras
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../CMSIS/Core/Include
        ${CMAKE_CURRENT_LIST_DIR}/../CMSIS/DSP/Include
)

# Add any user requested libraries
target_link_libraries(Pico2UltraHiResUSBDDC 
        hardware_pio 
        usb_device 
        pico_multicore
        hardware_clocks
        hardware_dma
        hardware_pwm
        pico_audio
        pico_audio_i2s
        hardware_uart
        hardware_i2c
)

pico_add_extra_outputs(Pico2UltraHiResUSBDDC)