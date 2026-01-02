/*
 * SD 卡日志模块实现
 *
 * 使用步骤：
 *   1. CubeMX 已启用 FATFS（User-defined 盘）和 SPI1，并生成：
 *        - FATFS/App/fatfs.c / fatfs.h
 *        - FATFS/Target/user_diskio.c
 *   2. main.c 里在 MX_FATFS_Init() 之后调用 SD_Card_Init()
 *   3. 每次采集到传感器数据后调用 SD_Card_Log() 追加记录
 *
 * 注意：
 *   - 这里仅负责文件层（FatFs），底层扇区读写需要你在
 *     FATFS/Target/user_diskio.c 中实现 SPI-SD 驱动。
 */

#include "sdcard.h"

#include "fatfs.h"
#include <stdio.h>
#include <string.h>

/* 日志文件句柄和状态标志 */
static FIL   s_logFile;
static uint8_t s_logOpened = 0;

int SD_Card_Init(void)
{
    FRESULT res;

    /* 挂载文件系统（逻辑盘路径由 CubeMX 在 fatfs.c 里生成） */
    res = f_mount(&USERFatFS, USERPath, 1);
    if (res != FR_OK)
    {
        printf("SD init: f_mount error=%d\r\n", res);
        return res;
    }

    /* 打开/创建日志文件 data.csv（根目录） */
    res = f_open(&s_logFile, "data.csv", FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        printf("SD init: f_open error=%d\r\n", res);
        return res;
    }

    /* 移到文件末尾，准备追加写入 */
    res = f_lseek(&s_logFile, f_size(&s_logFile));
    if (res != FR_OK)
    {
        f_close(&s_logFile);
        printf("SD init: f_lseek error=%d\r\n", res);
        return res;
    }

    s_logOpened = 1;

    /* 如果是新建文件，可以写一行表头，方便后续在 Excel / Python 里分析 */
    if (f_size(&s_logFile) == 0)
    {
        static const char header[] = "PH,TDS,TEMP,TURB\r\n";
        UINT bw = 0;
        res = f_write(&s_logFile, header, sizeof(header) - 1U, &bw);
        if (res != FR_OK || bw != (UINT)(sizeof(header) - 1U))
        {
            f_close(&s_logFile);
            s_logOpened = 0;
            printf("SD init: write header error=%d, bw=%u\r\n", res, bw);
            return res ? res : -1;
        }
        f_sync(&s_logFile);
    }

    return 0;
}

int SD_Card_Log(float ph, float tds, float temp, float turb)
{
    if (!s_logOpened)
    {
        printf("SD log: file not opened\r\n");
        return -1;
    }

    char buf[64];
    UINT bw = 0;

    int len = snprintf(buf, sizeof(buf),
                       "%.2f,%.0f,%.2f,%.2f\r\n",
                       ph, tds, temp, turb);
    if (len <= 0 || len >= (int)sizeof(buf))
    {
        return -2;
    }

    FRESULT res = f_write(&s_logFile, buf, (UINT)len, &bw);
    if (res != FR_OK || bw != (UINT)len)
    {
        printf("SD log: f_write error=%d, bw=%u, len=%d\r\n", res, bw, len);
        return res ? res : -3;
    }

    /* 为避免掉电丢数据，这里每次写入后都强制刷盘。
     * 若更关注寿命/速度，可以改成定期 f_sync。 */
    f_sync(&s_logFile);

    return 0;
}

void SD_Card_Deinit(void)
{
    if (s_logOpened)
    {
        f_sync(&s_logFile);
        f_close(&s_logFile);
        s_logOpened = 0;
    }

    /* 卸载文件系统 */
    f_mount(NULL, USERPath, 1);
}
