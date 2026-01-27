/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "fatfs.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ph.h"
#include "oled.h"
#include "ds18b20.h"
#include "sdcard.h"
#include <stdio.h>
#include "tds.h"
#include "turbidity.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* 浊度百分比映射上限（TU 对应 100%），可根据标定调整 */
#define TURB_MAX_TU   3000.0f

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 本工程只用几个全局句柄（ADC / I2C / UART），
 * 传感器参数尽量放到各自模块里维护，避免在 main 里到处散落 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 重定向 printf 到 USART1，方便串口调试 */
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

/* 统一保存当前一次采集到的所有水质参数 */
typedef struct
{
  float ph;        /* pH 值 */
  float temp_c;    /* 温度 ℃，来自 DS18B20 */
  float tds_ppm;   /* TDS ppm，如果论文不用，可以忽略 */
  float turbidity; /* 浊度 TU */
} SensorData_t;

/* 全局一份当前数据 */
static SensorData_t g_sensorData;
static uint32_t g_sdLogCounter = 0;

/**
 * @brief  读取所有传感器数据
 * @param  data  输出结构体指针
 * @param  K     浊度标定公式中的截距参数
 * @note   后续如果你增加溶解氧、电导率，可以在这里一并采集
 */
static void App_ReadSensors(SensorData_t *data, float K)
{
  if (data == NULL) return;

  /* 1. pH，内部已经做了电压转 pH 以及简单滤波 */
  data->ph = PH_ReadPH();

  /* 2. 温度，异常值暂时由显示函数处理 */
  data->temp_c = DS18B20_GetTemperature();

  /* 3. TDS，如果论文暂时不写 TDS，可以只保留 ph / turbidity / temp */
  data->tds_ppm = TDS_ReadPPM();

  /* 4. 浊度：先读电压，再用带温度补偿的公式计算 TU */
  float turb_v = Turbidity_ReadVoltage();
  data->turbidity = Turbidity_Calc(turb_v, data->temp_c, K);
}

/**
 * @brief  更新 OLED 上的显示内容
 * @param  data  输入的水质参数
 * @note   OLED 使用 8 行（page），这里每两行显示一项
 */
static void App_UpdateDisplay(const SensorData_t *data)
{
  if (data == NULL) return;

  char line[24];

  /* 第 0 行：pH 值 */
  snprintf(line, sizeof(line), "pH: %.2f", (double)data->ph);
  OLED_PrintLarge(0, 0, line);

  /* 第 2 行：温度，异常值用 -- 占位 */
  if (data->temp_c < -50.0f || data->temp_c > 125.0f)
  {
    snprintf(line, sizeof(line), "T: --");
  }
  else
  {
    snprintf(line, sizeof(line), "T: %.1fC", (double)data->temp_c);
  }
  OLED_PrintLarge(0, 2, line);

  /* 第 4 行：TDS（如果不用可以删掉这两行） */
  snprintf(line, sizeof(line), "TDS: %.0fppm", (double)data->tds_ppm);
  OLED_PrintLarge(0, 4, line);

  /* 第 6 行：浊度 0~100% 等级显示
   * 这里把 TU 按 0~TURB_MAX_TU 映射到 0~100%，并保留一位小数 */
  float turb_level = data->turbidity * 100.0f / TURB_MAX_TU;
  if (turb_level < 0.0f)   turb_level = 0.0f;
  if (turb_level > 100.0f) turb_level = 100.0f;
  snprintf(line, sizeof(line), "T: %4.1f%%", (double)turb_level);
  OLED_PrintLarge(0, 6, line);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
  /* 初始化 OLED 显示屏（I2C 接 I2C1） */
  OLED_Init();
  OLED_Clear();

  /* 初始化 DS18B20 温度传感器，数据脚接在 PB6 */
  uint8_t ds18b20_ok = DS18B20_Init();
  if (ds18b20_ok != 0U)
  {
    /* 如果初始化失败，OLED 上提示一下，但不中断主程序 */
    OLED_PrintLarge(0, 0, "TEMP ERR");
  }

  /* 浊度公式：TU = -865.68 * U25 + K
   * 其中 K 为你实测标定得到的截距，这里先给一个默认值。
   * 后续你做浊度标定实验时，可以把拟合出来的 K 写到这里。*/
  float K = 3200.0f;

  /* 初始化 SD 卡与文件系统（FatFs）并打开数据日志文件 */
  int sd_ok = SD_Card_Init();
  if (sd_ok != 0)
  {
    /* 若 SD 卡初始化失败，不影响主功能，仅在屏幕上提示 */
    OLED_PrintLarge(0, 6, "SD ERR");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 周期性任务：采集 -> 显示 -> 通过串口发送一帧数据给上位机 */
    App_ReadSensors(&g_sensorData, K);
    App_UpdateDisplay(&g_sensorData);

    /* 每 5 秒向 SD 卡追加一帧数据 */
    g_sdLogCounter++;
    if (g_sdLogCounter >= 5)
    {
      SD_Card_Log(g_sensorData.ph,
                  g_sensorData.tds_ppm,
                  g_sensorData.temp_c,
                  g_sensorData.turbidity);
      g_sdLogCounter = 0;
    }

    /* 串口统一输出一帧数据（方便 Python / PyQt5 上位机解析）：
     * 形如：
     *   PH=7.02;TEMP=25.3;TU=123.4;TDS=250\r\n
     * 你在上位机只需按 ';' 分割，再按 '=' 取值即可。
     */
    printf("PH=%.2f;TEMP=%.2f;TU=%.2f;TDS=%.0f\r\n",
           (double)g_sensorData.ph,
           (double)g_sensorData.temp_c,
           (double)g_sensorData.turbidity,
           (double)g_sensorData.tds_ppm);

    /* 采样周期：1 秒
     * 若以后需要更高实时性（例如 5Hz），可以把这个改小，
     * 或者用定时器中断/RTOS 来做，这在论文中也可以写成“改进方向”。 */
    HAL_Delay(1000);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
