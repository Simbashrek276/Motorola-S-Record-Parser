/*
 * srec.h - parser file Motorola S-record
 *
 * 1 dòng gồm 6 thành phần:
 *
 *   S  1  13  AF25  7C0802A69001...  6F
 *   |  |  |   |     |                |
 *   |  |  |   |     |                +-- Checksum
 *   |  |  |   |     +------------------- Data
 *   |  |  |   +------------------------- Address (2/3/4 byte, tuỳ Type)
 *   |  |  +----------------------------- Byte Count (số byte nằm SAU nó)
 *   |  +-------------------------------- Type
 *   +----------------------------------- luôn là 'S'
 */

#ifndef SREC_H
#define SREC_H

#include <stdint.h>

/* byte count tối đa 0xFF = 255, trừ địa chỉ (max 4 byte) và checksum (1 byte) */
#define SREC_MAX_DATA   250

/* 4 ký tự đầu + 255*2 ký tự thân + chỗ cho '\r' '\n' '\0' */
#define SREC_MAX_LINE   (4 + 255 * 2 + 8)

/* mã lỗi. Hàm nào trong srec.c cũng trả về kiểu này để biết sai ở đâu,
 * chứ không phải chỉ biết là sai */
typedef enum {
    SREC_OK = 0,
    SREC_ERR_NULL,
    SREC_ERR_EMPTY,
    SREC_ERR_NO_START,          /* không bắt đầu bằng 'S' */
    SREC_ERR_BAD_TYPE,          /* không phải 0-9, hoặc là S4 */
    SREC_ERR_BAD_HEX,
    SREC_ERR_TOO_SHORT,
    SREC_ERR_BAD_LENGTH,        /* độ dài dòng không khớp byte count */
    SREC_ERR_COUNT_TOO_SMALL,
    SREC_ERR_DATA_TOO_LONG,
    SREC_ERR_CHECKSUM
} srec_status_t;

/* kết quả parse 1 dòng. data[] để mảng cố định luôn, khỏi malloc cho đỡ rắc rối */
typedef struct {
    uint8_t  type;
    uint8_t  byte_count;
    uint32_t address;
    uint8_t  addr_len;              /* tính bằng byte, không phải ký tự */
    uint8_t  data[SREC_MAX_DATA];
    uint8_t  data_len;
    uint8_t  checksum;
} srec_record_t;

/* mỗi thành phần một hàm. Kết quả trả ra qua con trỏ, còn return để báo lỗi */
srec_status_t srec_check_start (const char *line);
srec_status_t srec_parse_type  (const char *line, uint8_t *type);
srec_status_t srec_parse_byte_count(const char *line, uint8_t *byte_count);
srec_status_t srec_parse_address(const char *line, uint8_t type,
                                 uint32_t *address, uint8_t *addr_len);
srec_status_t srec_parse_data  (const char *line, uint8_t byte_count,
                                uint8_t addr_len, uint8_t *data, uint8_t *data_len);
srec_status_t srec_parse_checksum(const char *line, uint8_t byte_count,
                                  uint8_t *checksum);
srec_status_t srec_verify_checksum(const char *line, const srec_record_t *rec);

/* gọi 6 hàm trên theo thứ tự, sai chỗ nào dừng luôn chỗ đó */
srec_status_t srec_parse_line(const char *line, srec_record_t *rec);

const char *srec_strerror(srec_status_t st);
int         srec_has_data(uint8_t type);    /* S1/S2/S3 mới có data */

#endif /* SREC_H */
