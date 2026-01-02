/*
 * 浊度采集与计算模块
 * - 模拟输入：PA1 (ADC1_IN1)，接浊度传感器的 AO
 * - 输出 1：Turbidity_ReadVoltage()  -> 经过中值 + 平均滤波后的电压 (V)
 * - 输出 2：Turbidity_Calc()        -> 根据电压 / 温度 / 标定截距计算 TU
 * - 输出 3：Turbidity_ReadTU()      -> 一步到位：内部完成采样 + 计算，返回 TU
 *
 * 标定思路（对应论文第七章“浊度标定实验”）：
 *   1. 准备几种已知浊度的溶液（例如 0 NTU、50 NTU、100 NTU、200 NTU …）
 *   2. 在 25℃ 左右，记录每种溶液对应的电压 U (V25)
 *   3. 以 TU 为纵轴、U 为横轴做拟合，得到线性关系 TU = -865.68 * U + K
 *   4. 将拟合出来的 K 写入 main.c 中的 K 变量，或保存在 Flash 里
 */

#include "turbidity.h"
#include "adc.h"
#include <stdio.h>

#define TURBIDITY_VREF        3.3f        // ADC 参考电压
#define TURBIDITY_ADC_MAX     4095.0f     // 12 位 ADC 最大值
#define TURBIDITY_READ_TIMES  10U         // 每次取 10 次样本做滤波

/* 如果你希望关闭串口调试输出，可以把下面这个宏改成 0 */
#define TURBIDITY_DEBUG_PRINT 1

extern ADC_HandleTypeDef hadc1;

/**
 * @brief  读一次 PA1 的 ADC 原始值
 * @retval 0~4095
 */
static uint16_t Turbidity_ReadAdcOnce(void)
{
    uint16_t value = 0;
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel      = ADC_CHANNEL_1;               // PA1
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
        return 0;

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
    {
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return value;
}

/* 简单的升序排序，用于中值滤波 */
static void Turbidity_SortU16(uint16_t *buf, uint8_t len)
{
    if (buf == NULL || len < 2) return;
    for (uint8_t i = 0; i < len - 1; i++)
    {
        for (uint8_t j = 0; j < len - 1 - i; j++)
        {
            if (buf[j] > buf[j + 1])
            {
                uint16_t tmp = buf[j];
                buf[j] = buf[j + 1];
                buf[j + 1] = tmp;
            }
        }
    }
}

/**
 * @brief  多次采样 + 中值平均滤波，返回电压值
 * @note   采样流程：
 *         1. 连续读取 TURBIDITY_READ_TIMES 次 ADC
 *         2. 对样本排序
 *         3. 丢弃最大值和最小值，取中间样本的平均
 */
float Turbidity_ReadVoltage(void)
{
    uint16_t samples[TURBIDITY_READ_TIMES] = {0};
    uint32_t raw_sum = 0;

    for (uint8_t i = 0; i < TURBIDITY_READ_TIMES; i++)
    {
        samples[i] = Turbidity_ReadAdcOnce();
        raw_sum += samples[i];
        HAL_Delay(5); // 采样间隔，避免过快
    }

    /* 计算未滤波的平均值，仅用于调试观察 */
    float raw_avg = (float)raw_sum / (float)TURBIDITY_READ_TIMES;

    /* 中值平均滤波：丢弃一个最大值和一个最小值，然后取平均 */
    Turbidity_SortU16(samples, TURBIDITY_READ_TIMES);

    uint32_t mid_sum = 0;
    uint8_t  mid_cnt = 0;

    if (TURBIDITY_READ_TIMES > 2)
    {
        for (uint8_t i = 1; i < TURBIDITY_READ_TIMES - 1; i++)
        {
            mid_sum += samples[i];
        }
        mid_cnt = TURBIDITY_READ_TIMES - 2;
    }
    else
    {
        /* 样本太少，就直接用原始平均 */
        mid_sum = (uint32_t)raw_avg * TURBIDITY_READ_TIMES;
        mid_cnt = TURBIDITY_READ_TIMES;
    }

    float avg = (float)mid_sum / (float)mid_cnt;

#if TURBIDITY_DEBUG_PRINT
    /* 串口打印平均 ADC 原始值和滤波后的值，便于在 PC 串口助手观察 */
    printf("TURBIDITY_ADC_RAW=%.0f, FILT=%.0f\r\n",
           (double)raw_avg, (double)avg);
#endif

    return (avg / TURBIDITY_ADC_MAX) * TURBIDITY_VREF;
}

/**
 * @brief  根据电压和温度计算浊度 TU
 * @param  voltage 当前 ADC 换算出的电压值 (V)
 * @param  temp    当前水温 (°C)，没有温度传感器可以传入 25.0f
 * @param  K       标定得到的截距值
 * @retval 浊度 TU（若计算结果小于 0，则返回 0）
 *
 * 计算步骤：
 *   1. 先做温度补偿：U25 = U - ΔU，ΔU = -0.0192 × (T - 25)
 *   2. 再代入线性标定公式：TU = -865.68 × U25 + K
 */
float Turbidity_Calc(float voltage, float temp, float K)
{
    /* 合理性保护：电压不应超过参考电压 */
    if (voltage < 0.0f)             voltage = 0.0f;
    if (voltage > TURBIDITY_VREF)   voltage = TURBIDITY_VREF;

    /* 温度补偿：ΔU = -0.0192 × (T - 25) */
    float deltaU = -0.0192f * (temp - 25.0f);
    float U25    = voltage - deltaU;      // 等效 25℃ 的电压

    /* 标定公式：TU = -865.68 × U25 + K */
    float tu = -865.68f * U25 + K;
    if (tu < 0.0f) tu = 0.0f;

    return tu;
}

/**
 * @brief  便捷函数：直接读 ADC 并计算 TU
 * @param  temp 当前水温 (°C)
 * @param  K    标定截距
 * @retval 浊度 TU
 */
float Turbidity_ReadTU(float temp, float K)
{
    float v = Turbidity_ReadVoltage();
    return Turbidity_Calc(v, temp, K);
}
