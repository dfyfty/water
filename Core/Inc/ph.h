#ifndef __PH_H
#define __PH_H

#include "stm32f1xx_hal.h"

// 读取 pH 传感器电压（已经做了 10k:20k 分压补偿）
// 返回单位：伏特（V），大概在 0~5V 之间
float PH_ReadVoltage(void);

// 读取并计算 pH 值（用内部的线性公式）
// 返回 0~14 之间的 pH 值
float PH_ReadPH(void);

// 设置标定系数：pH = k * V + b
// 标定后可以把算出来的 k、b 写进去
void PH_SetCalibration(float k, float b);


#endif
