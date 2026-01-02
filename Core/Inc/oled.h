#ifndef __OLED_H
#define __OLED_H

#include "stm32f1xx_hal.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_SetCursor(uint8_t column, uint8_t page);
void OLED_Print(uint8_t column, uint8_t page, const char *text);
void OLED_PrintLarge(uint8_t column, uint8_t page, const char *text);

#endif
