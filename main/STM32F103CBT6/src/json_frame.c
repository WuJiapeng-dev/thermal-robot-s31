#include "json_frame.h"
#include <string.h>

#define JSON_MAX_DEPTH 8U

typedef struct {
    const char *text;
    uint32_t length;
    uint32_t pos;
} json_cursor_t;

static void skip_space(json_cursor_t *c)
{
    while (c->pos < c->length) {
        char ch = c->text[c->pos];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') { break; }
        ++c->pos;
    }
}

static uint8_t take(json_cursor_t *c, char expected)
{
    skip_space(c);
    if (c->pos >= c->length || c->text[c->pos] != expected) { return 0U; }
    ++c->pos;
    return 1U;
}

static uint8_t hex_digit(char ch, uint8_t *value)
{
    if (ch >= '0' && ch <= '9') { *value = (uint8_t)(ch - '0'); return 1U; }
    if (ch >= 'a' && ch <= 'f') { *value = (uint8_t)(ch - 'a' + 10); return 1U; }
    if (ch >= 'A' && ch <= 'F') { *value = (uint8_t)(ch - 'A' + 10); return 1U; }
    return 0U;
}

static void emit_byte(char *out, uint32_t size, uint32_t *used, char ch, uint8_t *ok)
{
    if (out != 0) {
        if (*used + 1U >= size) { *ok = 0U; return; }
        out[*used] = ch;
    }
    ++*used;
}

static uint8_t parse_hex4(json_cursor_t *c, uint32_t *value)
{
    uint8_t i;
    uint8_t digit;
    *value = 0U;
    if (c->length - c->pos < 4U) { return 0U; }
    for (i = 0U; i < 4U; ++i) {
        if (!hex_digit(c->text[c->pos++], &digit)) { return 0U; }
        *value = (*value << 4) | digit;
    }
    return 1U;
}

static void emit_utf8(uint32_t cp, char *out, uint32_t size, uint32_t *used, uint8_t *ok)
{
    if (cp <= 0x7FU) {
        emit_byte(out, size, used, (char)cp, ok);
    } else if (cp <= 0x7FFU) {
        emit_byte(out, size, used, (char)(0xC0U | (cp >> 6)), ok);
        emit_byte(out, size, used, (char)(0x80U | (cp & 0x3FU)), ok);
    } else {
        emit_byte(out, size, used, (char)(0xE0U | (cp >> 12)), ok);
        emit_byte(out, size, used, (char)(0x80U | ((cp >> 6) & 0x3FU)), ok);
        emit_byte(out, size, used, (char)(0x80U | (cp & 0x3FU)), ok);
    }
}

static uint8_t parse_string(json_cursor_t *c, char *out, uint32_t size)
{
    uint32_t used = 0U;
    uint8_t ok = 1U;
    skip_space(c);
    if (c->pos >= c->length || c->text[c->pos++] != '"') { return 0U; }
    while (c->pos < c->length) {
        uint8_t ch = (uint8_t)c->text[c->pos++];
        if (ch == '"') {
            if (out != 0 && size > 0U) {
                if (used >= size) { return 0U; }
                out[used] = '\0';
            }
            return ok;
        }
        if (ch < 0x20U) { return 0U; }
        if (ch == '\\') {
            uint32_t cp;
            if (c->pos >= c->length) { return 0U; }
            ch = (uint8_t)c->text[c->pos++];
            switch (ch) {
            case '"': case '\\': case '/': emit_byte(out, size, &used, (char)ch, &ok); break;
            case 'b': emit_byte(out, size, &used, '\b', &ok); break;
            case 'f': emit_byte(out, size, &used, '\f', &ok); break;
            case 'n': emit_byte(out, size, &used, '\n', &ok); break;
            case 'r': emit_byte(out, size, &used, '\r', &ok); break;
            case 't': emit_byte(out, size, &used, '\t', &ok); break;
            case 'u':
                if (!parse_hex4(c, &cp) || (cp >= 0xD800U && cp <= 0xDFFFU)) { return 0U; }
                emit_utf8(cp, out, size, &used, &ok);
                break;
            default: return 0U;
            }
        } else {
            emit_byte(out, size, &used, (char)ch, &ok);
        }
    }
    return 0U;
}

static uint8_t parse_number(json_cursor_t *c, float *value)
{
    int sign = 1;
    int exponent_sign = 1;
    int exponent = 0;
    uint8_t digits = 0U;
    float number = 0.0f;
    float scale = 0.1f;
    skip_space(c);
    if (c->pos < c->length && c->text[c->pos] == '-') { sign = -1; ++c->pos; }
    while (c->pos < c->length && c->text[c->pos] >= '0' && c->text[c->pos] <= '9') {
        number = number * 10.0f + (float)(c->text[c->pos++] - '0');
        digits = 1U;
    }
    if (c->pos < c->length && c->text[c->pos] == '.') {
        ++c->pos;
        while (c->pos < c->length && c->text[c->pos] >= '0' && c->text[c->pos] <= '9') {
            number += (float)(c->text[c->pos++] - '0') * scale;
            scale *= 0.1f;
            digits = 1U;
        }
    }
    if (!digits) { return 0U; }
    if (c->pos < c->length && (c->text[c->pos] == 'e' || c->text[c->pos] == 'E')) {
        ++c->pos;
        if (c->pos < c->length && (c->text[c->pos] == '+' || c->text[c->pos] == '-')) {
            if (c->text[c->pos++] == '-') { exponent_sign = -1; }
        }
        digits = 0U;
        while (c->pos < c->length && c->text[c->pos] >= '0' && c->text[c->pos] <= '9') {
            if (exponent < 38) { exponent = exponent * 10 + (c->text[c->pos] - '0'); }
            ++c->pos;
            digits = 1U;
        }
        if (!digits) { return 0U; }
        while (exponent-- > 0) {
            number = (exponent_sign > 0) ? number * 10.0f : number * 0.1f;
        }
    }
    *value = sign > 0 ? number : -number;
    return 1U;
}

static uint8_t skip_value(json_cursor_t *c, uint8_t depth)
{
    char ch;
    if (depth > JSON_MAX_DEPTH) { return 0U; }
    skip_space(c);
    if (c->pos >= c->length) { return 0U; }
    ch = c->text[c->pos];
    if (ch == '"') { return parse_string(c, 0, 0U); }
    if (ch == '{' || ch == '[') {
        char close = ch == '{' ? '}' : ']';
        ++c->pos;
        skip_space(c);
        if (c->pos < c->length && c->text[c->pos] == close) { ++c->pos; return 1U; }
        for (;;) {
            if (ch == '{' && !parse_string(c, 0, 0U)) { return 0U; }
            if (ch == '{' && !take(c, ':')) { return 0U; }
            if (!skip_value(c, (uint8_t)(depth + 1U))) { return 0U; }
            skip_space(c);
            if (c->pos < c->length && c->text[c->pos] == ',') { ++c->pos; continue; }
            if (c->pos < c->length && c->text[c->pos] == close) { ++c->pos; return 1U; }
            return 0U;
        }
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        float ignored;
        return parse_number(c, &ignored);
    }
    if (c->length - c->pos >= 4U && memcmp(&c->text[c->pos], "true", 4U) == 0) {
        c->pos += 4U; return 1U;
    }
    if (c->length - c->pos >= 5U && memcmp(&c->text[c->pos], "false", 5U) == 0) {
        c->pos += 5U; return 1U;
    }
    if (c->length - c->pos >= 4U && memcmp(&c->text[c->pos], "null", 4U) == 0) {
        c->pos += 4U; return 1U;
    }
    return 0U;
}

uint8_t json_parse_frame(const char *text, uint32_t length, json_frame_t *out)
{
    json_cursor_t c;
    char type[16] = "";
    uint32_t seen = 0U;
    uint8_t ok = 1U;

    if (text == 0 || out == 0 || length == 0U) { return 0U; }
    memset(out, 0, sizeof(*out));
    out->id = -1;
    out->t = TEMPERATURE_INVALID;
    (void)strcpy(out->grp, "--");
    (void)strcpy(out->zh, "--");
    (void)strcpy(out->stage, "--");
    c.text = text; c.length = length; c.pos = 0U;
    if (!take(&c, '{')) { return 0U; }
    skip_space(&c);
    if (c.pos < c.length && c.text[c.pos] == '}') { return 0U; }

    while (ok && c.pos < c.length) {
        char key[16];
        float number;
        if (!parse_string(&c, key, sizeof(key)) || !take(&c, ':')) { return 0U; }
        if (strcmp(key, "type") == 0) {
            ok = parse_string(&c, type, sizeof(type)); seen |= JSON_MISSING_TYPE;
        } else if (strcmp(key, "grp") == 0) {
            ok = parse_string(&c, out->grp, sizeof(out->grp)); seen |= JSON_MISSING_GRP;
        } else if (strcmp(key, "zh") == 0) {
            ok = parse_string(&c, out->zh, sizeof(out->zh)); seen |= JSON_MISSING_ZH;
        } else if (strcmp(key, "stage") == 0) {
            ok = parse_string(&c, out->stage, sizeof(out->stage)); seen |= JSON_MISSING_STAGE;
        } else if (strcmp(key, "id") == 0 || strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
                   strcmp(key, "t") == 0 || strcmp(key, "dt") == 0 || strcmp(key, "px") == 0 ||
                   strcmp(key, "conf") == 0 || strcmp(key, "ts") == 0 || strcmp(key, "n") == 0 ||
                   strcmp(key, "prog") == 0 || strcmp(key, "seq") == 0 ||
                   strcmp(key, "frames") == 0) {
            if (!parse_number(&c, &number)) { return 0U; }
            if (strcmp(key, "id") == 0) { out->id = (int32_t)number; seen |= JSON_MISSING_ID; }
            else if (strcmp(key, "x") == 0) { out->x = (int32_t)number; seen |= JSON_MISSING_X; }
            else if (strcmp(key, "y") == 0) { out->y = (int32_t)number; seen |= JSON_MISSING_Y; }
            else if (strcmp(key, "t") == 0) { out->t = number; seen |= JSON_MISSING_TEMP; }
            else if (strcmp(key, "dt") == 0) { out->dt = number; seen |= JSON_MISSING_DT; }
            else if (strcmp(key, "px") == 0) { out->px = (int32_t)number; seen |= JSON_MISSING_PX; }
            else if (strcmp(key, "conf") == 0) { out->conf = number; seen |= JSON_MISSING_CONF; }
            else if (strcmp(key, "ts") == 0) { out->ts = (uint32_t)number; seen |= JSON_MISSING_TS; }
            else if (strcmp(key, "n") == 0) { out->n = (int32_t)number; seen |= JSON_MISSING_N; }
            else if (strcmp(key, "prog") == 0) { out->prog = (int32_t)number; seen |= JSON_MISSING_PROG; }
            else if (strcmp(key, "seq") == 0) { out->seq = (uint32_t)number; seen |= JSON_MISSING_SEQ; }
            else { out->frames = (int32_t)number; seen |= JSON_MISSING_FRAMES; }
        } else {
            ok = skip_value(&c, 0U);
        }
        if (!ok) { return 0U; }
        skip_space(&c);
        if (c.pos < c.length && c.text[c.pos] == ',') { ++c.pos; continue; }
        if (c.pos < c.length && c.text[c.pos] == '}') { ++c.pos; break; }
        return 0U;
    }
    skip_space(&c);
    if (c.pos != c.length) { return 0U; }

    if ((seen & JSON_MISSING_TYPE) == 0U) { return 0U; }
    if (strcmp(type, "hot") == 0) {
        out->type = JSON_TYPE_HOT;
        out->missing_mask = (uint32_t)(~seen) & 0x03FEUL;
    } else if (strcmp(type, "hb") == 0) {
        out->type = JSON_TYPE_HB;
        out->missing_mask = (uint32_t)(~seen) & (JSON_MISSING_TS | JSON_MISSING_N);
    } else if (strcmp(type, "st") == 0) {
        out->type = JSON_TYPE_ST;
        out->missing_mask = (uint32_t)(~seen) & (JSON_MISSING_STAGE | JSON_MISSING_PROG);
    } else if (strcmp(type, "capture_done") == 0) {
        out->type = JSON_TYPE_CAPTURE_DONE;
        out->missing_mask = (uint32_t)(~seen) & (JSON_MISSING_SEQ | JSON_MISSING_FRAMES);
    } else {
        return 0U;
    }
    return 1U;
}
