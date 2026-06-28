# drDRO Firmware

Firmware for the drDRO rotary-controller / digital-readout board, based on the
STMicroelectronics **STM32F411CEU6** microcontroller. Built with **PlatformIO**
(`framework = stm32cube`, FreeRTOS + Modbus RTU slave).

This is the PlatformIO successor to the STM32CubeIDE project `rotary-controller-f4`.

## Build & flash

```sh
pio run                 # build
pio run -t upload       # flash via ST-Link
pio run -t clean        # clean
```

Output: `.pio/build/drdro_f411ce/firmware.{elf,bin,hex}`.

## What it does

- Reads 4 quadrature scales (TIM1–TIM4 in encoder mode).
- Drives a step/dir servo with trapezoidal accel/decel ramps (jog, indexing, and
  scale-synchronised motion) from a TIM9 interrupt.
- Exposes state and accepts commands over **Modbus RTU** (slave address 17,
  USART1 @ 115200 8N1).

## Migration status

See `migration_checklist.md` for progress and `migration_todo.md` for the design
reference and peripheral/pin map.

## License

MIT (see source headers). © Stefano Bertelli.
