/*
 * OLED 显示驱动（SSD1306 协议兼容）
 * - 接口：I2C1，SCL = PB8, SDA = PB9（见 i2c.c）
 * - OLED_Init()     初始化并清屏
 * - OLED_Clear()    全屏清零
 * - OLED_Print()    正常 1x 字符显示
 * - OLED_PrintLarge() 放大 2x 字符显示（用于标题/数值）
 */

#include "oled.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define OLED_I2C_ADDR    (0x3C << 1) // 常见0x3C地址，若不同请改
#define OLED_WIDTH       128
#define OLED_PAGES       8
#define OLED_FONT_WIDTH  5

typedef struct
{
    char ch;
    uint8_t rows[7]; // 每行5位有效
} Glyph5x7;

static const Glyph5x7 s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00}},
    {':', {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'0', {0x0E, 0x11, 0x15, 0x15, 0x11, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'S', {0x0E, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x0E}},
    {'M', {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}},
    {'p', {0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'r', {0x00, 0x0B, 0x0C, 0x08, 0x08, 0x08, 0x08}},
};

static const Glyph5x7 *GlyphFind(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++)
    {
        if (s_font[i].ch == c)
        {
            return &s_font[i];
        }
    }
    return &s_font[0]; // 空格
}

static HAL_StatusTypeDef OLED_WriteCommand(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buf, 2, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef OLED_WriteData(const uint8_t *data, size_t len)
{
    uint8_t tmp[17];
    size_t offset = 0;
    HAL_StatusTypeDef status = HAL_OK;

    while (offset < len && status == HAL_OK)
    {
        size_t chunk = len - offset;
        if (chunk > 16) chunk = 16;
        tmp[0] = 0x40;
        memcpy(&tmp[1], &data[offset], chunk);
        status = HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, tmp, chunk + 1, HAL_MAX_DELAY);
        offset += chunk;
    }
    return status;
}

void OLED_SetCursor(uint8_t column, uint8_t page)
{
    if (column >= OLED_WIDTH) column = 0;
    if (page >= OLED_PAGES) page = 0;

    OLED_WriteCommand(0xB0 | (page & 0x07));
    OLED_WriteCommand(0x00 | (column & 0x0F));
    OLED_WriteCommand(0x10 | (column >> 4));
}

void OLED_Clear(void)
{
    uint8_t zeros[16] = {0};
    for (uint8_t page = 0; page < OLED_PAGES; page++)
    {
        OLED_SetCursor(0, page);
        for (uint8_t col = 0; col < OLED_WIDTH; col += sizeof(zeros))
        {
            OLED_WriteData(zeros, sizeof(zeros));
        }
    }
}

void OLED_Init(void)
{
    HAL_Delay(100);
    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0x20); OLED_WriteCommand(0x02); // page addressing
    OLED_WriteCommand(0x81); OLED_WriteCommand(0x7F); // contrast
    OLED_WriteCommand(0xA1); // segment remap
    OLED_WriteCommand(0xC8); // COM scan direction
    OLED_WriteCommand(0xA6); // normal display
    OLED_WriteCommand(0xA8); OLED_WriteCommand(0x3F); // multiplex ratio 1/64
    OLED_WriteCommand(0xD3); OLED_WriteCommand(0x00); // display offset
    OLED_WriteCommand(0xD5); OLED_WriteCommand(0x80); // clock divide
    OLED_WriteCommand(0xD9); OLED_WriteCommand(0xF1); // pre-charge
    OLED_WriteCommand(0xDA); OLED_WriteCommand(0x12); // COM pins
    OLED_WriteCommand(0xDB); OLED_WriteCommand(0x40); // VCOMH deselect
    OLED_WriteCommand(0x8D); OLED_WriteCommand(0x14); // charge pump
    OLED_WriteCommand(0xAF);
    OLED_Clear();
}

static void OLED_DrawChar(uint8_t column, uint8_t page, char c)
{
    const Glyph5x7 *glyph = GlyphFind(c);
    uint8_t columns[OLED_FONT_WIDTH + 1] = {0};

    for (uint8_t col = 0; col < OLED_FONT_WIDTH; col++)
    {
        uint8_t bits = 0;
        for (uint8_t row = 0; row < 7; row++)
        {
            if (glyph->rows[row] & (1U << (OLED_FONT_WIDTH - 1 - col)))
            {
                bits |= (1U << row);
            }
        }
        columns[col] = bits;
    }

    OLED_SetCursor(column, page);
    OLED_WriteData(columns, sizeof(columns)); // 最后一列是空列
}

// 2x 放大：宽高各乘2，整体高度14行，占用2页
static void OLED_DrawChar2x(uint8_t column, uint8_t page, char c)
{
    const Glyph5x7 *glyph = GlyphFind(c);
    uint8_t small[OLED_FONT_WIDTH] = {0};

    for (uint8_t col = 0; col < OLED_FONT_WIDTH; col++)
    {
        uint8_t bits = 0;
        for (uint8_t row = 0; row < 7; row++)
        {
            if (glyph->rows[row] & (1U << (OLED_FONT_WIDTH - 1 - col)))
            {
                bits |= (1U << row);
            }
        }
        small[col] = bits;
    }

    uint8_t top[(OLED_FONT_WIDTH + 1) * 2] = {0};
    uint8_t bottom[(OLED_FONT_WIDTH + 1) * 2] = {0};
    uint8_t idx = 0;

    for (uint8_t col = 0; col < OLED_FONT_WIDTH; col++)
    {
        uint8_t upper = 0, lower = 0;
        for (uint8_t row = 0; row < 7; row++)
        {
            if (small[col] & (1U << row))
            {
                uint8_t r0 = row * 2;
                uint8_t r1 = row * 2 + 1;
                if (r0 < 8) upper |= (1U << r0); else lower |= (1U << (r0 - 8));
                if (r1 < 8) upper |= (1U << r1); else lower |= (1U << (r1 - 8));
            }
        }
        top[idx] = upper;
        top[idx + 1] = upper;
        bottom[idx] = lower;
        bottom[idx + 1] = lower;
        idx += 2;
    }

    // 末尾空白列，两列空白用于字间距
    top[idx] = top[idx + 1] = 0;
    bottom[idx] = bottom[idx + 1] = 0;

    OLED_SetCursor(column, page);
    OLED_WriteData(top, sizeof(top));
    OLED_SetCursor(column, page + 1);
    OLED_WriteData(bottom, sizeof(bottom));
}

void OLED_Print(uint8_t column, uint8_t page, const char *text)
{
    uint8_t x = column;
    while (*text && x + OLED_FONT_WIDTH < OLED_WIDTH)
    {
        OLED_DrawChar(x, page, *text++);
        x += OLED_FONT_WIDTH + 1;
    }
}

void OLED_PrintLarge(uint8_t column, uint8_t page, const char *text)
{
    uint8_t x = column;
    while (*text && x + (OLED_FONT_WIDTH * 2 + 2) < OLED_WIDTH)
    {
        OLED_DrawChar2x(x, page, *text++);
        x += (OLED_FONT_WIDTH * 2 + 2);
    }
}
