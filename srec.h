/*
 * srec.h - Motorola S-record parser
 *
 *   S  1  13  AF25  7C0802A69001...  6F
 *   |  |  |   |     |                |
 *   |  |  |   |     |                +-- checksum
 *   |  |  |   |     +------------------- data
 *   |  |  |   +------------------------- address (2/3/4 bytes, set by the type)
 *   |  |  +----------------------------- byte count (how many bytes FOLLOW it)
 *   |  +-------------------------------- type
 *   +----------------------------------- always 'S'
 */

#ifndef SREC_H
#define SREC_H

#include <stdint.h>

/* Byte count is one byte, so 255 bytes max after it. Minus a 4-byte address
 * and the checksum, that leaves 250 bytes of data. */
#define SREC_MAX_DATA   250

/* "S" + type + count + 255*2 payload chars, plus room for CRLF and '\0'. */
#define SREC_MAX_LINE   (4 + 255 * 2 + 8)

typedef enum {
    SREC_OK = 0,
    SREC_ERR_NULL,
    SREC_ERR_EMPTY,
    SREC_ERR_NO_START,
    SREC_ERR_BAD_TYPE,
    SREC_ERR_BAD_HEX,
    SREC_ERR_TOO_SHORT,
    SREC_ERR_BAD_LENGTH,
    SREC_ERR_COUNT_TOO_SMALL,
    SREC_ERR_DATA_TOO_LONG,
    SREC_ERR_CHECKSUM
} srec_status_t;

typedef struct {
    uint8_t  type;
    uint8_t  byte_count;
    uint32_t address;
    uint8_t  addr_len;               /* address size in bytes, not chars */
    uint8_t  data[SREC_MAX_DATA];
    uint8_t  data_len;
    uint8_t  checksum;
} srec_record_t;

/* One function per field of the line. */
srec_status_t srec_check_start (const char *line);
srec_status_t srec_parse_type  (const char *line, uint8_t *type);
srec_status_t srec_parse_byte_count(const char *line, uint8_t *byte_count);
srec_status_t srec_parse_address(const char *line, uint8_t type,
                                 uint32_t *address, uint8_t *addr_len);
srec_status_t srec_parse_data  (const char *line, uint8_t byte_count,
                                uint8_t addr_len, uint8_t *data, uint8_t *data_len);
srec_status_t srec_parse_checksum(const char *line, uint8_t byte_count,
                                  uint8_t *checksum);
srec_status_t srec_verify_checksum(const srec_record_t *rec);

/* Runs all of the above in order, stopping at the first failure. */
srec_status_t srec_parse_line(const char *line, srec_record_t *rec);

const char *srec_strerror(srec_status_t st);
int         srec_has_data(uint8_t type);   /* S1/S2/S3 carry payload, others don't */

#endif /* SREC_H */
