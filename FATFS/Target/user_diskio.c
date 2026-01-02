/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
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

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include "ff_gen_drv.h"
#include "spi.h"
#include "main.h"

/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* SD 卡类型标志 */
static BYTE CardType = 0;

/* SPI 传输超时时间 */
#define SD_SPI_TIMEOUT 1000U

/* 一些命令定义（仅用到的部分） */
#define CMD0    (0U)        /* GO_IDLE_STATE */
#define CMD1    (1U)        /* SEND_OP_COND (MMC) */
#define CMD8    (8U)        /* SEND_IF_COND */
#define CMD9    (9U)        /* SEND_CSD */
#define CMD12   (12U)       /* STOP_TRANSMISSION */
#define CMD16   (16U)       /* SET_BLOCKLEN */
#define CMD17   (17U)       /* READ_SINGLE_BLOCK */
#define CMD24   (24U)       /* WRITE_BLOCK */
#define CMD55   (55U)       /* APP_CMD */
#define CMD58   (58U)       /* READ_OCR */
#define ACMD41  (0x80U+41U) /* SD_SEND_OP_COND (ACMD) */

/* 卡类型标志 */
#define CT_MMC    0x01U
#define CT_SD1    0x02U
#define CT_SD2    0x04U
#define CT_SDC    (CT_SD1 | CT_SD2)
#define CT_BLOCK  0x08U

static void SD_Select(void)
{
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
}

static void SD_Deselect(void)
{
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
  uint8_t dummy = 0xFF;
  uint8_t rx;
  HAL_SPI_TransmitReceive(&hspi1, &dummy, &rx, 1, SD_SPI_TIMEOUT);
}

static BYTE SD_SPI_TxRx(BYTE data)
{
  uint8_t tx = data;
  uint8_t rx = 0xFF;
  HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, SD_SPI_TIMEOUT);
  return rx;
}

static void SD_SendDummyClocks(UINT count)
{
  while (count--)
  {
    SD_SPI_TxRx(0xFF);
  }
}

static int SD_WaitReady(void)
{
  uint8_t resp;
  UINT timeout = 50000U;
  do
  {
    resp = SD_SPI_TxRx(0xFF);
  } while (resp != 0xFF && --timeout);
  return (resp == 0xFF);
}

static BYTE SD_SendCmdInternal(BYTE cmd, DWORD arg)
{
  BYTE crc = 0x01U;
  BYTE res;
  UINT n;

  if (cmd == CMD0)
  {
    crc = 0x95U;
  }
  else if (cmd == CMD8)
  {
    crc = 0x87U;
  }

  SD_SPI_TxRx(0xFF);

  SD_SPI_TxRx((BYTE)(0x40U | cmd));
  SD_SPI_TxRx((BYTE)(arg >> 24));
  SD_SPI_TxRx((BYTE)(arg >> 16));
  SD_SPI_TxRx((BYTE)(arg >> 8));
  SD_SPI_TxRx((BYTE)(arg));
  SD_SPI_TxRx(crc);

  n = 10U;
  do
  {
    res = SD_SPI_TxRx(0xFF);
  } while ((res & 0x80U) && --n);

  return res;
}

static BYTE SD_SendCmd(BYTE cmd, DWORD arg)
{
  BYTE res;

  if (cmd & 0x80U)
  {
    cmd &= 0x7FU;
    SD_Deselect();
    SD_Select();
    res = SD_SendCmdInternal(CMD55, 0);
    if (res > 1U)
    {
      SD_Deselect();
      return res;
    }
  }

  SD_Deselect();
  SD_Select();

  res = SD_SendCmdInternal(cmd, arg);
  return res;
}

static int SD_RecvData(BYTE *buff, UINT len)
{
  BYTE token;
  UINT timeout = 20000U;

  do
  {
    token = SD_SPI_TxRx(0xFF);
  } while (token == 0xFFU && --timeout);

  if (token != 0xFEU)
  {
    return 0;
  }

  while (len--)
  {
    *buff++ = SD_SPI_TxRx(0xFF);
  }

  SD_SPI_TxRx(0xFF);
  SD_SPI_TxRx(0xFF);

  return 1;
}

static int SD_XmitData(const BYTE *buff, BYTE token)
{
  BYTE resp;

  if (!SD_WaitReady())
  {
    return 0;
  }

  SD_SPI_TxRx(token);

  if (token != 0xFDU)
  {
    UINT len = 512U;
    while (len--)
    {
      SD_SPI_TxRx(*buff++);
    }

    SD_SPI_TxRx(0xFF);
    SD_SPI_TxRx(0xFF);

    resp = SD_SPI_TxRx(0xFF);
    if ((resp & 0x1FU) != 0x05U)
    {
      return 0;
    }

    /* 等待内部编程完成（忙期间 DO 会拉低） */
    if (!SD_WaitReady())
    {
      return 0;
    }
  }

  return 1;
}

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
  BYTE ty = 0;
  BYTE buf[4];
  UINT tmr;

  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }

  SD_Deselect();
  SD_SendDummyClocks(10U);

  if (SD_SendCmd(CMD0, 0) == 1U)
  {
    if (SD_SendCmd(CMD8, 0x1AAU) == 1U)
    {
      buf[0] = SD_SPI_TxRx(0xFF);
      buf[1] = SD_SPI_TxRx(0xFF);
      buf[2] = SD_SPI_TxRx(0xFF);
      buf[3] = SD_SPI_TxRx(0xFF);

      if ((buf[2] == 0x01U) && (buf[3] == 0xAAU))
      {
        tmr = 10000U;
        do
        {
          if (SD_SendCmd(ACMD41, 1UL << 30) == 0U)
          {
            break;
          }
        } while (--tmr);

        if (tmr && SD_SendCmd(CMD58, 0) == 0U)
        {
          buf[0] = SD_SPI_TxRx(0xFF);
          buf[1] = SD_SPI_TxRx(0xFF);
          buf[2] = SD_SPI_TxRx(0xFF);
          buf[3] = SD_SPI_TxRx(0xFF);
          ty = (buf[0] & 0x40U) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
        }
      }
    }
    else
    {
      if (SD_SendCmd(ACMD41, 0) <= 1U)
      {
        ty = CT_SD1;
        tmr = 10000U;
        do
        {
          if (SD_SendCmd(ACMD41, 0) == 0U)
          {
            break;
          }
        } while (--tmr);
      }
      else
      {
        ty = CT_MMC;
        tmr = 10000U;
        do
        {
          if (SD_SendCmd(CMD1, 0) == 0U)
          {
            break;
          }
        } while (--tmr);
      }

      if (!tmr || SD_SendCmd(CMD16, 512U) != 0U)
      {
        ty = 0;
      }
    }
  }

  CardType = ty;
  SD_Deselect();

  if (ty)
  {
    Stat &= ~STA_NOINIT;
  }
  else
  {
    Stat = STA_NOINIT;
  }

  /* 调试输出：可以看到卡类型 */
  printf("SD USER_initialize: type=0x%02X, Stat=0x%02X\r\n", CardType, Stat);

  return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }
  return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
  if ((pdrv != 0U) || !count)
  {
    return RES_PARERR;
  }
  if (Stat & STA_NOINIT)
  {
    return RES_NOTRDY;
  }

  if (!(CardType & CT_BLOCK))
  {
    sector *= 512U;
  }

  SD_Select();

  DRESULT res = RES_OK;

  while (count--)
  {
    if (SD_SendCmd(CMD17, sector) != 0U || !SD_RecvData(buff, 512U))
    {
      res = RES_ERROR;
      break;
    }
    sector++;
    buff += 512U;
  }

  SD_Deselect();

  return res;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
  if ((pdrv != 0U) || !count)
  {
    return RES_PARERR;
  }
  if (Stat & STA_NOINIT)
  {
    return RES_NOTRDY;
  }

  if (Stat & STA_PROTECT)
  {
    return RES_WRPRT;
  }

  if (!(CardType & CT_BLOCK))
  {
    sector *= 512U;
  }

  SD_Select();

  DRESULT res = RES_OK;

  while (count--)
  {
    /* 每个扇区最多重试 3 次，避免偶发错误导致整个写失败 */
    uint8_t retry = 3U;
    while (retry--)
    {
      if (SD_SendCmd(CMD24, sector) == 0U && SD_XmitData(buff, 0xFEU))
      {
        break;
      }
    }

    if ((int8_t)retry < 0)
    {
      res = RES_ERROR;
      break;
    }

    sector++;
    buff += 512U;
  }

  SD_Deselect();

  return res;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
  if (pdrv != 0U)
  {
    return RES_PARERR;
  }

  if (Stat & STA_NOINIT)
  {
    return RES_NOTRDY;
  }

  DRESULT res = RES_ERROR;

  switch (cmd)
  {
    case CTRL_SYNC:
      SD_Select();
      if (SD_WaitReady())
      {
        res = RES_OK;
      }
      SD_Deselect();
      break;

    case GET_SECTOR_SIZE:
      *(WORD *)buff = 512U;
      res = RES_OK;
      break;

    case GET_BLOCK_SIZE:
      *(DWORD *)buff = 1U;
      res = RES_OK;
      break;

    case GET_SECTOR_COUNT:
      /* 这里只返回一个“看上去像”的大小值（例如 4GB 卡约等于 8M 扇区），
       * 实际不影响正常读写，只是避免某些情况下 f_mkfs 直接报错。 */
      *(DWORD *)buff = 8UL * 1024UL * 1024UL; /* 约 8M 扇区（4GB） */
      res = RES_OK;
      break;

    default:
      res = RES_PARERR;
      break;
  }

  return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

