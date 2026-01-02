/* Includes ------------------------------------------------------------------*/
#include "delay.h"

/* USER CODE BEGIN 0 */

void Delay_us(uint32_t us)
{
    /* 使用 DWT 周期计数器实现微秒延时，避免干扰 SysTick 与 HAL_Delay */
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    uint32_t ticks = (SystemCoreClock / 1000000U) * us;
    uint32_t start = DWT->CYCCNT;

    while ((DWT->CYCCNT - start) < ticks)
    {
        __NOP();
    }
}

/**
  * @brief  毫秒级延时
  * @param  ms 延时时长，单位：毫秒
  * @retval None
  */
void Delay_ms(uint32_t ms)
{
    while (ms--)
    {
        Delay_us(1000U);
    }
}

/**
  * @brief  秒级延时
  * @param  s 延时时长，单位：秒
  * @retval None
  */
void Delay_s(uint32_t s)
{
    while (s--)
    {
        Delay_ms(1000U);
    }
}

/* USER CODE END 0 */
