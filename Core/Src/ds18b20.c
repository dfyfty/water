/*
 * DS18B20 温度传感器驱动
 * - 数据脚接在 PB6（见 ds18b20.h 的宏定义）
 * - DS18B20_Init()          初始化总线
 * - DS18B20_GetTemperature() 读取当前温度（°C）
 */

#include "ds18b20.h"
#include "delay.h"

#define DS18B20_DQ_HIGH()   (DS18B20_PORT->BSRR = DS18B20_PIN)
#define DS18B20_DQ_LOW()    (DS18B20_PORT->BRR  = DS18B20_PIN)
#define DS18B20_DQ_READ()   ((DS18B20_PORT->IDR & DS18B20_PIN) ? 1U : 0U)

/* USER CODE BEGIN 0 */

// 把 PB6 配成上拉输入（读取总线电平）
static void DS18B20_IO_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
}

// 把 PB6 配成推挽输出（驱动总线）
static void DS18B20_IO_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
    DS18B20_DQ_HIGH();
}

void DS18B20_Reset(void)
{
    DS18B20_IO_OUT();
    DS18B20_DQ_LOW();
    Delay_us(750);
    DS18B20_DQ_HIGH();
    Delay_us(15);
}

uint8_t DS18B20_Check(void)
{
    uint8_t retry = 0;
    DS18B20_IO_IN();
    while (DS18B20_DQ_READ() && retry < 200)
    {
        retry++;
        Delay_us(1);
    }
    if (retry >= 200)
        return 1;
    else
        retry = 0;
    while (!DS18B20_DQ_READ() && retry < 240)
    {
        retry++;
        Delay_us(1);
    }
    if (retry >= 240)
        return 1;
    return 0;
}

uint8_t DS18B20_Read_Bit(void)
{
    uint8_t data;
    DS18B20_IO_OUT();
    DS18B20_DQ_LOW();
    Delay_us(2);
    DS18B20_DQ_HIGH();
    DS18B20_IO_IN();
    Delay_us(12);
    data = DS18B20_DQ_READ() ? 1U : 0U;
    Delay_us(50);
    return data;
}

uint8_t DS18B20_Read_Byte(void)
{
    uint8_t i, j, dat = 0;
    for (i = 1; i <= 8; i++)
    {
        j = DS18B20_Read_Bit();
        dat = (uint8_t)((j << 7) | (dat >> 1));
    }
    return dat;
}

static void DS18B20_Write_Byte(uint8_t dat)
{
    uint8_t j;
    uint8_t testb;
    DS18B20_IO_OUT();
    for (j = 1; j <= 8; j++)
    {
        testb = (uint8_t)(dat & 0x01U);
        dat >>= 1;
        if (testb)
        {
            DS18B20_DQ_LOW();
            Delay_us(2);
            DS18B20_DQ_HIGH();
            Delay_us(60);
        }
        else
        {
            DS18B20_DQ_LOW();
            Delay_us(60);
            DS18B20_DQ_HIGH();
            Delay_us(2);
        }
    }
}

void DS18B20_Start(void)
{
    DS18B20_Reset();
    DS18B20_Check();
    DS18B20_Write_Byte(0xCC);
    DS18B20_Write_Byte(0x44);
}

uint8_t DS18B20_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);

    DS18B20_DQ_HIGH();
    Delay_us(10);

    DS18B20_Reset();
    return DS18B20_Check();
}

uint16_t DS18B20_GetRaw(void)
{
    uint16_t temp;
    uint8_t a, b;
    DS18B20_Start();
    Delay_ms(750);
    DS18B20_Reset();
    DS18B20_Check();
    DS18B20_Write_Byte(0xCC);
    DS18B20_Write_Byte(0xBE);
    a = DS18B20_Read_Byte();
    b = DS18B20_Read_Byte();
    temp = (uint16_t)((b << 8) | a);
    return temp;
}

float DS18B20_GetTemperature(void)
{
    uint16_t temp = DS18B20_GetRaw();
    float value;
    if ((temp & 0xF800U) == 0xF800U)
    {
        temp = (uint16_t)(~temp + 1U);
        value = (float)temp * (-0.0625f);
    }
    else
    {
        value = (float)temp * 0.0625f;
    }
    return value;
}

/* USER CODE END 0 */
