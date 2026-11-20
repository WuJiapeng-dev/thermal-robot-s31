#ifndef ILI9341_H
#define ILI9341_H

#include <stdint.h>

#define LCD_COLOR_BLACK   0x0000U
#define LCD_COLOR_WHITE   0xFFFFU
#define LCD_COLOR_RED     0xF800U
#define LCD_COLOR_GREEN   0x07E0U
#define LCD_COLOR_BLUE    0x001FU
#define LCD_COLOR_YELLOW  0xFFE0U
#define LCD_COLOR_CYAN    0x07FFU

void lcd_init(void);
void lcd_fill_screen(uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void lcd_draw_string(uint16_t x, uint16_t y, const char *text,
                     uint16_t fg, uint16_t bg, uint8_t scale);
void lcd_test_pattern(void);

#endif
