#ifndef __TURBIDITY_H__
#define __TURBIDITY_H__

#include "stm32f1xx_hal.h"

/**
 * @brief  多次采样 + 中值平均滤波，返回等效电压值 (V)
 */
float Turbidity_ReadVoltage(void);

/**
 * @brief  根据电压和温度计算浊度 TU
 * @param  voltage 当前电压 (V)
 * @param  temp    当前水温 (°C)
 * @param  K       标定截距
 * @retval 浊度 TU
 */
float Turbidity_Calc(float voltage, float temp, float K);

/**
 * @brief  一步到位：内部完成采样 + 计算，返回浊度 TU
 * @param  temp 当前水温 (°C)，例如 DS18B20_GetTemperature() 返回值
 * @param  K    标定截距
 */
float Turbidity_ReadTU(float temp, float K);

#endif
