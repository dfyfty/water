/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ds18b20.h
  * @brief   This file contains the DS18B20 temperature sensor driver
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DS18B20_H
#define __DS18B20_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Private defines */

/* DS18B20 端口和引脚定义：PB6 */
#define DS18B20_PORT        GPIOB
#define DS18B20_PIN         GPIO_PIN_6

/* USER CODE END Private defines */

/* USER CODE BEGIN Prototypes */
/* 对外只暴露两个接口，其他细节都在 ds18b20.c 内部实现 */
uint8_t DS18B20_Init(void);          // 初始化 PB6 上的 DS18B20
float   DS18B20_GetTemperature(void); // 读取当前温度，单位：°C
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __DS18B20_H */
