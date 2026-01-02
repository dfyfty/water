/*
 * SD 卡日志模块（基于 FatFs）
 * - 负责把每次采集到的 pH / TDS / 温度 / 浊度 追加写入 data.csv
 * - 底层存储介质由 FATFS/App/fatfs.c + FATFS/Target/user_diskio.c 提供
 */

#ifndef PH_SDCARD_H
#define PH_SDCARD_H

#include "stm32f1xx_hal.h"

/*
 * 初始化 SD 卡与文件系统，并打开/创建日志文件 data.csv。
 * 返回值：
 *   0     - 成功
 *   其它  - FatFs 错误码（FRESULT）或负数表示本模块内部错误
 */
int SD_Card_Init(void);

/*
 * 记录一条数据：pH / TDS / 温度 / 浊度。
 * 返回值：
 *   0     - 成功
 *   其它  - 错误
 */
int SD_Card_Log(float ph, float tds, float temp, float turb);

/*
 * 关闭日志文件并卸载文件系统，可在系统关闭前调用（可选）。
 */
void SD_Card_Deinit(void);

#endif // PH_SDCARD_H
