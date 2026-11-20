#ifndef JSON_FRAME_H
#define JSON_FRAME_H

#include <stdint.h>
#include "board_config.h"

typedef enum {
    JSON_TYPE_NONE = 0,
    JSON_TYPE_HOT,
    JSON_TYPE_HB,
    JSON_TYPE_ST,
    JSON_TYPE_CAPTURE_DONE
} json_message_type_t;

typedef struct {
    json_message_type_t type;
    int32_t id;
    char grp[JSON_GROUP_CHARS];
    char zh[JSON_ZH_CHARS];
    int32_t x;
    int32_t y;
    float t;
    float dt;
    int32_t px;
    float conf;
    uint32_t ts;
    int32_t n;
    char stage[JSON_STAGE_CHARS];
    int32_t prog;
    uint32_t seq;
    int32_t frames;
    uint32_t missing_mask;
} json_frame_t;

enum {
    JSON_MISSING_TYPE = 1UL << 0,
    JSON_MISSING_ID   = 1UL << 1,
    JSON_MISSING_TEMP = 1UL << 2,
    JSON_MISSING_GRP  = 1UL << 3,
    JSON_MISSING_ZH   = 1UL << 4,
    JSON_MISSING_X    = 1UL << 5,
    JSON_MISSING_Y    = 1UL << 6,
    JSON_MISSING_DT   = 1UL << 7,
    JSON_MISSING_PX   = 1UL << 8,
    JSON_MISSING_CONF = 1UL << 9,
    JSON_MISSING_TS   = 1UL << 10,
    JSON_MISSING_N    = 1UL << 11,
    JSON_MISSING_STAGE = 1UL << 12,
    JSON_MISSING_PROG = 1UL << 13,
    JSON_MISSING_SEQ  = 1UL << 14,
    JSON_MISSING_FRAMES = 1UL << 15
};

uint8_t json_parse_frame(const char *text, uint32_t length, json_frame_t *out);

#endif
