#include "ph.h"
#include <stddef.h>

/*
 * pH 采集与计算模块
 * - 模拟输入：PA2 (ADC1_IN2)，接 pH 传感器 AO
 * - 输出 1：PH_ReadVoltage() -> 探头电压 (V)
 * - 输出 2：PH_ReadPH()      -> 0~14 的 pH 值（带简单滤波）
 */

// 这些参数你可以以后再改
#define PH_VREF      3.3f        // STM32 ADC 参考电压
#define PH_ADC_MAX   4095.0f     // 12位 ADC 最大值
// 按手册测得：pH6.86≈1.7V，pH4≈2.2V，pH9.18≈1.3V，模块输出已在0~3.3V范围，默认不再做分压补偿
#define PH_DIV_GAIN  1.0f        // 如果外部做了分压，这里可以再还原

// 默认的线性公式：pH = k * V + b
// 不同模块可能不一样，以后可以通过 PH_SetCalibration 调整
static float s_ph_k = -5.7541f;
static float s_ph_b = 16.654f;
static uint8_t s_custom_cal = 0;

typedef struct {
    float v;
    float ph;
} PhPoint;

// 按手册提供的三点做分段插值，减少探头非线性带来的误差
static const PhPoint s_cal_points[] = {
    {2.2f, 4.00f},
    {1.7f, 6.86f},
    {1.3f, 9.18f},
};

// 滑动平均缓冲，平滑最终 pH 值
#define PH_MA_LEN 8
static float s_ph_ma[PH_MA_LEN] = {0};
static uint8_t s_ph_ma_pos = 0;
static uint8_t s_ph_ma_filled = 0;

// 这个变量在 adc.c / MX_ADC1_Init 里定义，这里用 extern 声明
extern ADC_HandleTypeDef hadc1;

// 内部函数：读一次 PA2 的 ADC 原始值
static uint16_t PH_ReadAdcOnce(void)
{
    uint16_t value = 0;
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = ADC_CHANNEL_2;               // PA2
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
        return 0;

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return value;
}


// 内部函数：做简单多次平均，减小抖动
static uint16_t PH_ReadAdcAverage(uint8_t times)
{
    uint32_t sum = 0;
    if (times == 0) times = 1;

    for (uint8_t i = 0; i < times; i++)
    {
        sum += PH_ReadAdcOnce();
    }
    return (uint16_t)(sum / times);
}

// 对外：读取电压（V），已经做了分压补偿
float PH_ReadVoltage(void)
{
    uint16_t adc = PH_ReadAdcAverage(10); // 取10次平均
    float v = (float)adc * PH_VREF / PH_ADC_MAX; // MCU 脚上的电压（0~3.3V）

    // 补偿分压，把它还原为传感器输出的真实电压（0~5V 左右）
    v *= PH_DIV_GAIN;

    return v;
}

// 对外：读取 pH 值
float PH_ReadPH(void)
{
    float v  = PH_ReadVoltage();
    float ph = 0.0f;

    if (s_custom_cal)
    {
        ph = s_ph_k * v + s_ph_b;
    }
    else
    {
        // 分段线性插值（按电压由高到低排序）
        const size_t n = sizeof(s_cal_points) / sizeof(s_cal_points[0]);

        if (v >= s_cal_points[0].v)
        {
            // 高于最高点，向外推
            float k = (s_cal_points[1].ph - s_cal_points[0].ph) / (s_cal_points[1].v - s_cal_points[0].v);
            float b = s_cal_points[0].ph - k * s_cal_points[0].v;
            ph = k * v + b;
        }
        else if (v <= s_cal_points[n - 1].v)
        {
            // 低于最低点，向外推
            float k = (s_cal_points[n - 1].ph - s_cal_points[n - 2].ph) / (s_cal_points[n - 1].v - s_cal_points[n - 2].v);
            float b = s_cal_points[n - 1].ph - k * s_cal_points[n - 1].v;
            ph = k * v + b;
        }
        else
        {
            for (size_t i = 0; i < n - 1; i++)
            {
                float v_hi = s_cal_points[i].v;
                float v_lo = s_cal_points[i + 1].v;
                if (v <= v_hi && v >= v_lo)
                {
                    float k = (s_cal_points[i + 1].ph - s_cal_points[i].ph) / (v_lo - v_hi);
                    float b = s_cal_points[i].ph - k * v_hi;
                    ph = k * v + b;
                    break;
                }
            }
        }
    }

    // 简单限幅到 0~14
    if (ph < 0.0f)  ph = 0.0f;
    if (ph > 14.0f) ph = 14.0f;

    // 滑动平均滤波
    s_ph_ma[s_ph_ma_pos] = ph;
    s_ph_ma_pos = (s_ph_ma_pos + 1) % PH_MA_LEN;
    if (s_ph_ma_filled < PH_MA_LEN) s_ph_ma_filled++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < s_ph_ma_filled; i++) sum += s_ph_ma[i];
    return sum / (float)s_ph_ma_filled;
}


// 对外：设置标定参数
void PH_SetCalibration(float k, float b)
{
    s_ph_k = k;
    s_ph_b = b;
    s_custom_cal = 1;
}
