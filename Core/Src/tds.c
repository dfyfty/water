/*
 * TDS（总溶解固体）采集与计算模块
 * - 模拟输入：PA0 (ADC1_IN0)，接 TDS 传感器 AO
 * - 输出 1：TDS_ReadVoltage() -> 电压 (V)
 * - 输出 2：TDS_ReadPPM()     -> TDS（ppm）
 */

#include "../Inc/tds.h"
#include "adc.h"

#define TDS_VREF        3.3f          // ADC 参考电压
#define TDS_ADC_MAX     4095.0f       // 12 位 ADC 最大值
#define TDS_READ_TIMES  10U           // 每次取 10 次平均

extern ADC_HandleTypeDef hadc1;

static uint16_t TDS_ReadAdcOnce(void)
{
    uint16_t value = 0;
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = ADC_CHANNEL_0;               // PA0
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
        return 0;

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return value;
}

float TDS_ReadVoltage(void)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < TDS_READ_TIMES; i++)
    {
        sum += TDS_ReadAdcOnce();
        HAL_Delay(5); // 简单延时，减小抖动
    }
    float avg = (float)sum / (float)TDS_READ_TIMES;
    return (avg / TDS_ADC_MAX) * TDS_VREF;
}

float TDS_ReadPPM(void)
{
    float v = TDS_ReadVoltage();
    float tds = 66.71f * v * v * v - 127.93f * v * v + 428.7f * v;
    if (tds < 20.0f) tds = 0.0f;
    return tds;
}
