//
// Created by stefano on 12/30/22.
//

#ifndef ROTARY_CONTROLLER_F4_SCALES_H
#define ROTARY_CONTROLLER_F4_SCALES_H
#include "stm32f4xx_hal.h"

#define SCALES_COUNT 4

/* TIM input-capture filter (CCMR1 ICxF) is a 4-bit field: 0 (off) .. 15 (max). */
#define SCALES_FILTER_MAX 15U

HAL_StatusTypeDef initScaleTimer(TIM_HandleTypeDef * timHandle, uint16_t filter);

/* Reprogram the encoder input filter (both channels) on a running timer. */
void setScaleFilter(TIM_HandleTypeDef * timHandle, uint16_t filter);
#endif //ROTARY_CONTROLLER_F4_SCALES_H
