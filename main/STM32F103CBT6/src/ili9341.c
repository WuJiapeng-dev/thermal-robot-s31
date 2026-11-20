#include "ili9341.h"
#include "board_config.h"
#include "font12.h"
#include "timebase.h"

static uint8_t g_lcd_line[LCD_LINE_BUFFER_BYTES];

/* 经典 5x7 ASCII 点阵，列存储、bit0 在顶部；单元格为 6x8。 */
static const uint8_t g_ascii5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x10,0x08,0x08,0x10,0x08},{0x00,0x00,0x00,0x00,0x00}
};

static uint8_t lcd_spi_byte(uint8_t value)
{
    uint32_t timeout = LCD_SPI_TIMEOUT;
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_TXE) == RESET) {
        if (--timeout == 0U) { return 0U; }
    }
    SPI_I2S_SendData(LCD_SPI, value);
    timeout = LCD_SPI_TIMEOUT;
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_BSY) == SET) {
        if (--timeout == 0U) { return 0U; }
    }
    return 1U;
}

static void lcd_select(uint8_t selected)
{
    GPIO_WriteBit(LCD_CS_PORT, LCD_CS_PIN, selected ? Bit_RESET : Bit_SET);
}

static void lcd_command(uint8_t command)
{
    lcd_select(1U);
    GPIO_ResetBits(LCD_DC_PORT, LCD_DC_PIN);
    (void)lcd_spi_byte(command);
    lcd_select(0U);
}

static void lcd_data(uint8_t value)
{
    lcd_select(1U);
    GPIO_SetBits(LCD_DC_PORT, LCD_DC_PIN);
    (void)lcd_spi_byte(value);
    lcd_select(0U);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_command(0x2AU);
    lcd_data((uint8_t)(x0 >> 8)); lcd_data((uint8_t)x0);
    lcd_data((uint8_t)(x1 >> 8)); lcd_data((uint8_t)x1);
    lcd_command(0x2BU);
    lcd_data((uint8_t)(y0 >> 8)); lcd_data((uint8_t)y0);
    lcd_data((uint8_t)(y1 >> 8)); lcd_data((uint8_t)y1);
    lcd_command(0x2CU);
}

void lcd_init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;

    RCC_APB2PeriphClockCmd(LCD_SPI_CLK | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = LCD_SCK_PIN;
    GPIO_Init(LCD_SCK_PORT, &gpio);
    gpio.GPIO_Pin = LCD_MOSI_PIN;
    GPIO_Init(LCD_MOSI_PORT, &gpio);

    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pin = LCD_CS_PIN | LCD_DC_PIN | LCD_RST_PIN | LCD_BL_PIN;
    GPIO_Init(GPIOB, &gpio);
    lcd_select(0U);
    GPIO_ResetBits(LCD_BL_PORT, LCD_BL_PIN);

    SPI_I2S_DeInit(LCD_SPI);
    SPI_StructInit(&spi);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = LCD_SPI_BAUD_DIV;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;
    SPI_Init(LCD_SPI, &spi);
    SPI_Cmd(LCD_SPI, ENABLE);

    GPIO_ResetBits(LCD_RST_PORT, LCD_RST_PIN);
    timebase_delay_ms(LCD_RESET_LOW_MS);
    GPIO_SetBits(LCD_RST_PORT, LCD_RST_PIN);
    timebase_delay_ms(LCD_RESET_HIGH_MS);

    lcd_command(0x01U);                 /* Soft Reset */
    timebase_delay_ms(5U);
    lcd_command(0x11U);                 /* Sleep Out */
    timebase_delay_ms(120U);
    lcd_command(0x3AU); lcd_data(0x55U); /* 16bit RGB565 */
    lcd_command(0x36U); lcd_data(0x48U); /* Portrait, MX + BGR */
    lcd_command(0x29U);                 /* Display ON */
    GPIO_SetBits(LCD_BL_PORT, LCD_BL_PIN);
    lcd_fill_screen(LCD_COLOR_BLACK);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t row;
    uint16_t i;
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    if ((x >= LCD_WIDTH) || (y >= LCD_HEIGHT) || (w == 0U) || (h == 0U)) { return; }
    if ((uint32_t)x + w > LCD_WIDTH) { w = LCD_WIDTH - x; }
    if ((uint32_t)y + h > LCD_HEIGHT) { h = LCD_HEIGHT - y; }
    for (i = 0U; i < w; ++i) {
        g_lcd_line[i * 2U] = hi;
        g_lcd_line[i * 2U + 1U] = lo;
    }
    lcd_set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    lcd_select(1U);
    GPIO_SetBits(LCD_DC_PORT, LCD_DC_PIN);
    for (row = 0U; row < h; ++row) {
        for (i = 0U; i < (uint16_t)(w * 2U); ++i) {
            if (lcd_spi_byte(g_lcd_line[i]) == 0U) {
                lcd_select(0U);
                return;
            }
        }
    }
    lcd_select(0U);
}

void lcd_fill_screen(uint16_t color)
{
    lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    lcd_fill_rect(x, y, w, 1U, color);
}

static void lcd_draw_ascii(uint16_t x, uint16_t y, char ch, uint16_t fg,
                           uint16_t bg, uint8_t scale)
{
    uint8_t row;
    uint8_t col;
    uint8_t index = ((uint8_t)ch >= 32U && (uint8_t)ch <= 127U) ?
                    ((uint8_t)ch - 32U) : (uint8_t)('?' - 32U);

    lcd_fill_rect(x, y, (uint16_t)(6U * scale), (uint16_t)(8U * scale), bg);
    for (col = 0U; col < 5U; ++col) {
        for (row = 0U; row < 7U; ++row) {
            if ((g_ascii5x7[index][col] & (uint8_t)(1U << row)) != 0U) {
                lcd_fill_rect((uint16_t)(x + col * scale), (uint16_t)(y + row * scale),
                              scale, scale, fg);
            }
        }
    }
}

static uint32_t utf8_decode(const uint8_t *s, uint8_t *consumed)
{
    if (s[0] < 0x80U) { *consumed = 1U; return s[0]; }
    if ((s[0] & 0xE0U) == 0xC0U && (s[1] & 0xC0U) == 0x80U) {
        *consumed = 2U;
        return ((uint32_t)(s[0] & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
    }
    if ((s[0] & 0xF0U) == 0xE0U && (s[1] & 0xC0U) == 0x80U &&
        (s[2] & 0xC0U) == 0x80U) {
        *consumed = 3U;
        return ((uint32_t)(s[0] & 0x0FU) << 12) |
               ((uint32_t)(s[1] & 0x3FU) << 6) | (uint32_t)(s[2] & 0x3FU);
    }
    if ((s[0] & 0xF8U) == 0xF0U && (s[1] & 0xC0U) == 0x80U &&
        (s[2] & 0xC0U) == 0x80U && (s[3] & 0xC0U) == 0x80U) {
        *consumed = 4U;
        return ((uint32_t)(s[0] & 0x07U) << 18) |
               ((uint32_t)(s[1] & 0x3FU) << 12) |
               ((uint32_t)(s[2] & 0x3FU) << 6) | (uint32_t)(s[3] & 0x3FU);
    }
    *consumed = 1U;
    return 0xFFFFFFFFUL;
}

static void lcd_draw_chinese(uint16_t x, uint16_t y, const uint8_t *glyph,
                             uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint8_t row;
    uint8_t col;
    uint16_t bits;

    lcd_fill_rect(x, y, (uint16_t)(12U * scale), (uint16_t)(12U * scale), bg);
    if (glyph == 0) {
        lcd_draw_ascii((uint16_t)(x + 3U * scale), (uint16_t)(y + 2U * scale),
                       '?', fg, bg, scale);
        return;
    }
    for (row = 0U; row < 12U; ++row) {
        bits = (uint16_t)(((uint16_t)glyph[row * 2U] << 8) | glyph[row * 2U + 1U]);
        for (col = 0U; col < 12U; ++col) {
            if ((bits & (uint16_t)(0x8000U >> col)) != 0U) {
                lcd_fill_rect((uint16_t)(x + col * scale), (uint16_t)(y + row * scale),
                              scale, scale, fg);
            }
        }
    }
}

void lcd_draw_string(uint16_t x, uint16_t y, const char *text,
                     uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint32_t cursor = x;
    const uint8_t *s = (const uint8_t *)text;
    uint8_t consumed;
    uint32_t codepoint;
    const uint8_t *glyph;

    if ((text == 0) || (scale == 0U)) { return; }
    while ((*s != 0U) && (cursor < LCD_WIDTH)) {
        codepoint = utf8_decode(s, &consumed);
        s += consumed;
        if (codepoint < 0x80U) {
            lcd_draw_ascii((uint16_t)cursor, y, (char)codepoint, fg, bg, scale);
            cursor += 6U * scale;
        } else {
            glyph = (codepoint == 0xFFFFFFFFUL) ? 0 : font12_get_unicode(codepoint);
            lcd_draw_chinese((uint16_t)cursor, y, glyph, fg, bg, scale);
            cursor += 12U * scale;
        }
    }
}

void lcd_test_pattern(void)
{
    lcd_fill_screen(LCD_COLOR_BLACK);
    lcd_fill_rect(0U, 0U, LCD_WIDTH / 3U, 40U, LCD_COLOR_RED);
    lcd_fill_rect(LCD_WIDTH / 3U, 0U, LCD_WIDTH / 3U, 40U, LCD_COLOR_GREEN);
    lcd_fill_rect((LCD_WIDTH / 3U) * 2U, 0U, LCD_WIDTH / 3U, 40U, LCD_COLOR_BLUE);
    lcd_draw_string(8U, 56U, "ILI9341 LCD TEST", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 2U);
    lcd_draw_string(8U, 88U, "ASCII 0123456789", LCD_COLOR_CYAN, LCD_COLOR_BLACK, 1U);
    lcd_draw_string(8U, 108U, "Chinese fallback: 电源区", LCD_COLOR_YELLOW,
                    LCD_COLOR_BLACK, 1U);
    lcd_draw_hline(0U, 140U, LCD_WIDTH, LCD_COLOR_WHITE);
}
