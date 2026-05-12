# Vulcan OS

A minimal real-time operating system for STM32H7 (Cortex-M7), built entirely in-house for running and training ML models on the edge.

## Architecture

```
vulcan/
├── include/          # public headers
│   ├── vulcan.h      # core types, config, register defs
│   ├── vulcan_task.h # TCB, scheduler API
│   ├── vulcan_mem.h  # pool allocator, tensor arena
│   └── vulcan_mutex.h
├── kernel/           # OS implementation
│   ├── vulcan_sched.c  # PendSV context switch, SysTick, scheduler
│   ├── vulcan_mem.c    # pool + arena allocators
│   └── vulcan_mutex.c  # LDREX/STREX binary mutex
├── hal/
│   └── startup_stm32h743.s  # vector table, Reset_Handler
├── ml/               # Phase 3: operator library (coming)
├── main.c            # demo: sensor + inference + report tasks
├── stm32h743.ld      # linker script (Flash/DTCM/SRAM1 layout)
├── CMakeLists.txt
└── cmake/
    └── arm-none-eabi.cmake
```

## Target

- **MCU**: STM32H743 — Cortex-M7 @ 480 MHz, 1 MB DTCM, 2 MB Flash
- **Toolchain**: `arm-none-eabi-gcc`

## Phase 1 — Kernel + Memory (implemented)

- Preemptive priority scheduler via PendSV + SysTick
- Cooperative yield (`vk_task_yield`) and timed sleep (`vk_task_sleep`)
- FPU-aware context switch (lazy save of S16–S31)
- O(1) pool allocator with intrusive free list
- Bump-pointer tensor arena mapped to DTCM (zero-wait-cycle)
- LDREX/STREX binary mutex with FIFO wait queue
- Stack canary watermarking per task

## Build

```bash
# Prerequisites
sudo apt install gcc-arm-none-eabi cmake

# Configure
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake

# Build
cmake --build build

# Flash (STLink)
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
        -c "program build/vulcan.bin 0x08000000 verify reset exit"
```

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | ✅ Done | Kernel, scheduler, memory, mutex |
| 2 | Planned | HAL — UART, DMA, SPI, ADC |
| 3 | Planned | ML ops — INT8 matmul, conv2d, activations |
| 4 | Planned | Model loader — Vulcan binary format |
| 5 | Planned | Training engine — SGD/Adam, INT8 gradients |

## License

MIT
