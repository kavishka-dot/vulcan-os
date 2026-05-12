cmake_minimum_required(VERSION 3.20)

# ── toolchain must be set before project() ─────────────────────────
# Build with:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#   cmake --build build

project(vulcan C ASM)
set(CMAKE_C_STANDARD 11)

# ── target MCU flags ────────────────────────────────────────────────
# STM32H743 = Cortex-M7 with double-precision FPU
set(MCU_FLAGS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

# ── compile flags ───────────────────────────────────────────────────
add_compile_options(
    ${MCU_FLAGS}
    -O2
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections          # allow linker to discard unused functions
    -fdata-sections
    -fno-exceptions
    -fno-unwind-tables
    -DSTM32H743xx
)

# ── linker flags ────────────────────────────────────────────────────
add_link_options(
    ${MCU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/stm32h743.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/vulcan.map
    --specs=nosys.specs           # no semihosting / C library syscalls
    --specs=nano.specs            # minimal newlib-nano
)

# ── sources ─────────────────────────────────────────────────────────
add_executable(vulcan.elf
    main.c
    kernel/vulcan_sched.c
    kernel/vulcan_mutex.c
    kernel/vulcan_mem.c
)

target_include_directories(vulcan.elf PRIVATE
    include
    hal                           # will expand in Phase 2
)

# ── post-build: generate .bin and .hex for flashing ─────────────────
add_custom_command(TARGET vulcan.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:vulcan.elf>
            ${CMAKE_BINARY_DIR}/vulcan.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:vulcan.elf>
            ${CMAKE_BINARY_DIR}/vulcan.hex
    COMMENT "Generating vulcan.bin and vulcan.hex"
)

# ── size report ─────────────────────────────────────────────────────
add_custom_command(TARGET vulcan.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vulcan.elf>
    COMMENT "Firmware size:"
)
