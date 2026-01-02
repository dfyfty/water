#ifndef __TDS_H
#define __TDS_H

#include "stm32f1xx_hal.h"

float TDS_ReadVoltage(void);   // PA0, ADC1_IN0 的电压 (V)
float TDS_ReadPPM(void);       // TDS 数值 (ppm)

#endif
