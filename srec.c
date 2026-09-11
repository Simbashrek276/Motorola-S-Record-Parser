/*
 * srec.c - parsing logic. Nothing here prints; the caller decides how to
 * report a status code. That keeps this file usable without stdio.
 */

#include <string.h>
#include "srec.h"

/* Every field sits at a fixed character offset, so we index instead of scan. */
#define OFF_START   0
#define OFF_TYPE    1
#define OFF_COUNT   2
#define OFF_ADDR    4

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* The file stores each byte as two ASCII characters, so "AF" -> 0xAF. */
static srec_status_t hex_to_byte(const char *p, uint8_t *out)
{
    int hi = hex_value(p[0]);
    int lo = hex_value(p[1]);

    if (hi < 0 || lo < 0) {
        return SREC_ERR_BAD_HEX;
    }
    *out = (uint8_t)((hi << 4) | lo);
    return SREC_OK;
}

static srec_status_t addr_len_of_type(uint8_t type, uint8_t *addr_len)
{
    switch (type) {
    case 0: case 1: case 5: case 9: *addr_len = 2; break;
    case 2: case 6: case 8:         *addr_len = 3; break;
    case 3: case 7:                 *addr_len = 4; break;
    default:                        return SREC_ERR_BAD_TYPE;   /* S4 is unused */
    }
    return SREC_OK;
}

srec_status_t srec_check_start(const char *line)
{
    if (line == NULL)    return SREC_ERR_NULL;
    if (line[0] == '\0') return SREC_ERR_EMPTY;
    if (line[0] != 'S' && line[0] != 's') return SREC_ERR_NO_START;
    return SREC_OK;
}

srec_status_t srec_parse_type(const char *line, uint8_t *type)
{
    char c;
    uint8_t dummy;

    if (line == NULL || type == NULL) return SREC_ERR_NULL;

    c = line[OFF_TYPE];
    if (c < '0' || c > '9') return SREC_ERR_BAD_TYPE;

    *type = (uint8_t)(c - '0');

    /* Borrow the address table to reject S4. */
    return addr_len_of_type(*type, &dummy);
}

srec_status_t srec_parse_byte_count(const char *line, uint8_t *byte_count)
{
    if (line == NULL || byte_count == NULL) return SREC_ERR_NULL;
    if (strlen(line) < OFF_COUNT + 2)       return SREC_ERR_TOO_SHORT;

    return hex_to_byte(&line[OFF_COUNT], byte_count);
}

srec_status_t srec_parse_address(const char *line, uint8_t type,
                                 uint32_t *address, uint8_t *addr_len)
{
    srec_status_t st;
    uint8_t len, i, b;
    uint32_t value = 0;

    if (line == NULL || address == NULL || addr_len == NULL) return SREC_ERR_NULL;

    st = addr_len_of_type(type, &len);
    if (st != SREC_OK) return st;

    if (strlen(line) < (size_t)(OFF_ADDR + len * 2)) return SREC_ERR_TOO_SHORT;

    /* Big-endian: first byte read is the most significant. */
    for (i = 0; i < len; i++) {
        st = hex_to_byte(&line[OFF_ADDR + i * 2], &b);
        if (st != SREC_OK) return st;
        value = (value << 8) | b;
    }

    *address  = value;
    *addr_len = len;
    return SREC_OK;
}

srec_status_t srec_parse_data(const char *line, uint8_t byte_count,
                              uint8_t addr_len, uint8_t *data, uint8_t *data_len)
{
    srec_status_t st;
    uint8_t n, i;
    size_t offset;

    if (line == NULL || data == NULL || data_len == NULL) return SREC_ERR_NULL;

    /* Byte count covers address + data + checksum, so data is the leftover. */
    if (byte_count < addr_len + 1) return SREC_ERR_COUNT_TOO_SMALL;
    n = (uint8_t)(byte_count - addr_len - 1);

    if (n > SREC_MAX_DATA) return SREC_ERR_DATA_TOO_LONG;

    offset = (size_t)OFF_ADDR + addr_len * 2;
    if (strlen(line) < offset + (size_t)n * 2) return SREC_ERR_TOO_SHORT;

    for (i = 0; i < n; i++) {
        st = hex_to_byte(&line[offset + i * 2], &data[i]);
        if (st != SREC_OK) return st;
    }

    *data_len = n;
    return SREC_OK;
}

srec_status_t srec_parse_checksum(const char *line, uint8_t byte_count,
                                  uint8_t *checksum)
{
    size_t expected_len;

    if (line == NULL || checksum == NULL) return SREC_ERR_NULL;

    /* This is also where a truncated or padded line gets caught. */
    expected_len = 4 + (size_t)byte_count * 2;
    if (strlen(line) != expected_len) return SREC_ERR_BAD_LENGTH;

    return hex_to_byte(&line[expected_len - 2], checksum);
}

srec_status_t srec_verify_checksum(const srec_record_t *rec)
{
    uint32_t sum = 0;
    uint8_t i, computed;

    if (rec == NULL) return SREC_ERR_NULL;

    sum += rec->byte_count;

    /* Split the address back into bytes, most significant first. */
    for (i = 0; i < rec->addr_len; i++) {
        sum += (rec->address >> (8 * (rec->addr_len - 1 - i))) & 0xFF;
    }

    for (i = 0; i < rec->data_len; i++) {
        sum += rec->data[i];
    }

    computed = (uint8_t)(0xFF - (sum & 0xFF));

    return (computed == rec->checksum) ? SREC_OK : SREC_ERR_CHECKSUM;
}

srec_status_t srec_parse_line(const char *line, srec_record_t *rec)
{
    srec_status_t st;

    if (line == NULL || rec == NULL) return SREC_ERR_NULL;

    memset(rec, 0, sizeof(*rec));

    st = srec_check_start(line);
    if (st != SREC_OK) return st;

    st = srec_parse_type(line, &rec->type);
    if (st != SREC_OK) return st;

    st = srec_parse_byte_count(line, &rec->byte_count);
    if (st != SREC_OK) return st;

    st = srec_parse_address(line, rec->type, &rec->address, &rec->addr_len);
    if (st != SREC_OK) return st;

    st = srec_parse_data(line, rec->byte_count, rec->addr_len,
                         rec->data, &rec->data_len);
    if (st != SREC_OK) return st;

    st = srec_parse_checksum(line, rec->byte_count, &rec->checksum);
    if (st != SREC_OK) return st;

    return srec_verify_checksum(rec);
}

const char *srec_strerror(srec_status_t st)
{
    switch (st) {
    case SREC_OK:                 return "OK";
    case SREC_ERR_NULL:           return "Internal error: NULL pointer";
    case SREC_ERR_EMPTY:          return "Empty line";
    case SREC_ERR_NO_START:       return "Line does not start with 'S'";
    case SREC_ERR_BAD_TYPE:       return "Invalid record type";
    case SREC_ERR_BAD_HEX:        return "Invalid hex character";
    case SREC_ERR_TOO_SHORT:      return "Line is too short";
    case SREC_ERR_BAD_LENGTH:     return "Line length does not match byte count";
    case SREC_ERR_COUNT_TOO_SMALL:return "Byte count is too small";
    case SREC_ERR_DATA_TOO_LONG:  return "Data field is too long";
    case SREC_ERR_CHECKSUM:       return "Checksum invalid!";
    default:                      return "Unknown error";
    }
}

int srec_has_data(uint8_t type)
{
    return (type == 1 || type == 2 || type == 3);
}
