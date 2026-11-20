#ifndef FONT12_H
#define FONT12_H

#include <stdint.h>

/* 返回 24 字节字模：逐行，每行 2 字节，高字节在前，低 4 位无效；缺字返回 0。 */
const uint8_t *font12_get(uint16_t gb2312);
const uint8_t *font12_get_unicode(uint32_t unicode);
uint16_t font12_catalog_size(void);
uint8_t font12_has_verified_glyphs(void);

#endif
