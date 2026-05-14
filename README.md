# Vulcan OS

A minimal real-time operating system for STM32H7 (Cortex-M7), written entirely in-house for running and training ML models on the edge. No FreeRTOS. No STM32 HAL. No TFLite. Every byte is ours.

## Architecture

```
vulcan/
├── include/               # public headers
│   ├── vulcan.h           # core types, config, register defs
│   ├── vulcan_task.h      # TCB, scheduler API
│   ├── vulcan_mem.h       # pool allocator, tensor arena
│   ├── vulcan_mutex.h     # binary mutex
│   ├── vulcan_clock.h     # PLL / sysclock
│   ├── vulcan_gpio.h      # GPIO
│   ├── vulcan_uart.h      # UART (interrupt TX, ring-buffer RX)
│   ├── vulcan_dma.h       # DMA1/DMA2 + DMAMUX1
│   ├── vulcan_adc.h       # ADC1/2 continuous DMA mode
│   └── vulcan_spi.h       # SPI master (polling + DMA)
├── kernel/                # OS kernel
│   ├── vulcan_sched.c     # PendSV context switch, SysTick, scheduler
│   ├── vulcan_mem.c       # pool + arena allocators
│   └── vulcan_mutex.c     # LDREX/STREX binary mutex
├── hal/                   # hardware abstraction layer
│   ├── startup_stm32h743.s  # vector table, Reset_Handler
│   ├── vulcan_clock.c     # PLL1 -> 480 MHz, DWT delay
│   ├── vulcan_gpio.c      # pin config, AF, atomic set/clear
│   ├── vulcan_dma.c       # stream init, DMAMUX routing, ISR
│   ├── vulcan_uart.c      # USART1/3, interrupt TX, VK_LOG macro
│   ├── vulcan_adc.c       # continuous DMA circular, batch callback
│   └── vulcan_spi.c       # SPI1/2/3 master, DMA burst
├── ml/                    # Phase 3: operator library (coming)
├── main.c                 # demo: sensor + inference + report tasks
├── stm32h743.ld           # linker script (Flash / DTCM / SRAM1)
├── CMakeLists.txt
└── cmake/arm-none-eabi.cmake
```

## Target

| Property | Value |
|----------|-------|
| MCU | STM32H743ZI |
| Core | Cortex-M7 @ 480 MHz |
| Flash | 2 MB |
| DTCM RAM | 128 KB (tensor arena lives here) |
| SRAM1 | 128 KB (OS + tasks) |
| Toolchain | arm-none-eabi-gcc |
| Board | Nucleo-H743ZI2 (default pinout) |

## Phases

| Phase | Status | What |
|-------|--------|------|
| 1 | Done | Kernel: preemptive scheduler, pool allocator, tensor arena, mutex |
| 2 | Done | HAL: SysClock 480 MHz, UART, GPIO, DMA, ADC continuous, SPI |
| 3 | Planned | ML ops: INT8 matmul (SMLAD), conv2d, activations, softmax |
| 4 | Planned | Model loader: Vulcan binary format |
| 5 | Planned | Training engine: SGD/Adam, INT8 gradients, micro-backprop |

## Build

```bash
sudo apt install gcc-arm-none-eabi cmake make

cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build

openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
        -c "program build/vulcan.bin 0x08000000 verify reset exit"
```

## Debug output (115200 8N1 on Nucleo virtual COM)

```
[VK] Vulcan OS -- Phase 2  STM32H743 @ 480000000 Hz
[VK] sensor task started
[VK] ADC1 continuous DMA started (CH4, CH5)
[VK] inference task started
[VK] report task started
[VK] SPI1 loopback: sent=0xA5 recv=0xA5
[VK] tick=5000  sensor_free=312  infer_free=289  arena_peak=128
```

## License

MIT
