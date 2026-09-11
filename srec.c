/*
 * srec.c - phần xử lý S-record
 *
 * File này không printf, không đụng tới file. Chỉ nhận chuỗi vào, parse,
 * rồi trả mã lỗi. In ra hay ghi file là việc của main.c
 */

#include <string.h>
#include "srec.h"

/* các trường có độ dài cố định nên nhảy thẳng tới vị trí cần đọc:
 *
 *   S 1 11 0038 48656C6C6F...
 *   ^ ^ ^  ^
 *   0 1 2  4
 */
#define OFF_START   0
#define OFF_TYPE    1
#define OFF_COUNT   2
#define OFF_ADDR    4

/* trả về -1 nếu không phải ký tự hex, nên để kiểu int chứ không uint8_t.
 * Trong ASCII '0'..'9' nằm liền nhau nên trừ '0' là ra số */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* 2 ký tự hex -> 1 byte. Trong file mỗi byte ghi bằng 2 ký tự ASCII, có chữ
 * 'A' và 'F' chứ không có sẵn số 0xAF.
 *
 * VD "AF": hi = 10, lo = 15, (hi << 4) | lo = 0xAF     (<< 4 là nhân 16)
 *
 * Mấy hàm parse bên dưới thực chất chỉ là vòng lặp gọi hàm này */
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

/* Type quyết định địa chỉ dài mấy byte. Chưa biết cái này thì chưa biết
 * data bắt đầu từ đâu. S4 chuẩn không định nghĩa nên loại luôn */
static srec_status_t addr_len_of_type(uint8_t type, uint8_t *addr_len)
{
    switch (type) {
    case 0: case 1: case 5: case 9: *addr_len = 2; break;
    case 2: case 6: case 8:         *addr_len = 3; break;
    case 3: case 7:                 *addr_len = 4; break;
    default:                        return SREC_ERR_BAD_TYPE;
    }
    return SREC_OK;
}

/* --- 1. ký tự 'S' --- */
srec_status_t srec_check_start(const char *line)
{
    /* phải check NULL xong mới được đụng vào line[0] */
    if (line == NULL)    return SREC_ERR_NULL;
    if (line[0] == '\0') return SREC_ERR_EMPTY;

    /* nhận cả 's' thường, vài tool cũ vẫn ghi kiểu đó */
    if (line[0] != 'S' && line[0] != 's') return SREC_ERR_NO_START;

    return SREC_OK;
}

/* --- 2. Type --- */
srec_status_t srec_parse_type(const char *line, uint8_t *type)
{
    char c;
    uint8_t unused_len;

    if (line == NULL || type == NULL) return SREC_ERR_NULL;

    c = line[OFF_TYPE];
    if (c < '0' || c > '9') return SREC_ERR_BAD_TYPE;

    *type = (uint8_t)(c - '0');

    /* mượn bảng tra ở trên để loại S4, độ dài địa chỉ ở đây chưa cần.
     * Làm vậy để danh sách type hợp lệ chỉ nằm một chỗ */
    return addr_len_of_type(*type, &unused_len);
}

/* --- 3. Byte Count --- */
srec_status_t srec_parse_byte_count(const char *line, uint8_t *byte_count)
{
    if (line == NULL || byte_count == NULL) return SREC_ERR_NULL;

    /* check đủ dài rồi mới đọc */
    if (strlen(line) < OFF_COUNT + 2) return SREC_ERR_TOO_SHORT;

    return hex_to_byte(&line[OFF_COUNT], byte_count);
}

/* --- 4. Address --- */
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

    /* big-endian, byte đọc trước là byte lớn nhất.
     * value = (value << 8) | b  ->  đẩy cái đang có sang trái 1 byte rồi
     * nhét byte mới vào chỗ trống:
     *
     *   0x0000 -> đọc AF -> 0x00AF -> đọc 25 -> 0xAF25
     *
     * dùng chung cho địa chỉ 2, 3, 4 byte, chỉ khác số vòng lặp */
    for (i = 0; i < len; i++) {
        st = hex_to_byte(&line[OFF_ADDR + i * 2], &b);
        if (st != SREC_OK) return st;

        value = (value << 8) | b;
    }

    *address  = value;
    *addr_len = len;
    return SREC_OK;
}

/* --- 5. Data --- */
srec_status_t srec_parse_data(const char *line, uint8_t byte_count,
                              uint8_t addr_len, uint8_t *data, uint8_t *data_len)
{
    srec_status_t st;
    uint8_t n, i;
    size_t offset;

    if (line == NULL || data == NULL || data_len == NULL) return SREC_ERR_NULL;

    /* byte count đếm cả địa chỉ + data + checksum, nên data là phần còn lại.
     *
     * Cái if dưới đây đừng bỏ: mấy biến này unsigned hết, nếu byte_count nhỏ
     * hơn thì phép trừ không ra số âm mà quay vòng thành số cực to (0-1 = 255),
     * xong vòng for đọc lố ra ngoài chuỗi luôn */
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

/* --- 6. Checksum (mới chỉ đọc ra thôi) --- */
srec_status_t srec_parse_checksum(const char *line, uint8_t byte_count,
                                  uint8_t *checksum)
{
    size_t expected_len;

    if (line == NULL || checksum == NULL) return SREC_ERR_NULL;

    /* dòng chuẩn dài đúng 4 + byte_count*2 ký tự.
     * Để "!=" chứ đừng để "<", dòng dài dư cũng là dòng hỏng */
    expected_len = 4 + (size_t)byte_count * 2;
    if (strlen(line) != expected_len) return SREC_ERR_BAD_LENGTH;

    /* checksum là 2 ký tự cuối */
    return hex_to_byte(&line[expected_len - 2], checksum);
}

srec_status_t srec_verify_checksum(const char *line, const srec_record_t *rec)
{
    srec_status_t st;
    uint32_t sum = 0;
    size_t i, checksum_pos;
    uint8_t b, computed;

    if (line == NULL || rec == NULL) return SREC_ERR_NULL;

    /* cách tính: cộng hết các byte từ Byte Count tới hết Data, lấy byte thấp
     * nhất của tổng rồi đảo lại.
     *
     * Chữ 'S', type và chính checksum không được cộng. Mà mấy cái đó nằm ở 2
     * đầu dòng (vị trí 0, 1 và 2 ký tự cuối), nên cứ chạy từ vị trí 2 tới
     * trước checksum là vừa đẹp.
     *
     * Hàm này chạy sau srec_parse_checksum() nên độ dài dòng chắc chắn đúng rồi */
    checksum_pos = 4 + (size_t)rec->byte_count * 2 - 2;

    for (i = OFF_COUNT; i < checksum_pos; i += 2) {
        st = hex_to_byte(&line[i], &b);
        if (st != SREC_OK) return st;

        sum += b;
    }

    /* & 0xFF giữ lại 8 bit thấp, 0xFF - x là đảo bit.
     * VD dòng S111003848656C6C6F20776F726C642E0A0042:
     *   tổng = 1213 = 0x4BD, & 0xFF = 0xBD, 0xFF - 0xBD = 0x42  -> khớp */
    computed = (uint8_t)(0xFF - (sum & 0xFF));

    if (computed != rec->checksum) {
        return SREC_ERR_CHECKSUM;
    }
    return SREC_OK;
}

srec_status_t srec_parse_line(const char *line, srec_record_t *rec)
{
    srec_status_t st;

    if (line == NULL || rec == NULL) return SREC_ERR_NULL;

    memset(rec, 0, sizeof(*rec));   /* xoá rác của lần parse trước */

    /* thứ tự này không đảo được:
     *   type -> biết địa chỉ dài mấy byte
     *   byte count + độ dài địa chỉ -> biết data dài mấy byte
     * Sai chỗ nào return luôn chỗ đó, dòng hỏng không đi sâu được vào trong */
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

    return srec_verify_checksum(line, rec);
}

/* trả về chuỗi cho main.c tự in, chứ hàm này không printf */
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

/* S0 là header, S5/S6 đếm số record, S7/S8/S9 báo hết file.
 * Mấy dòng đó vẫn check checksum nhưng không có data để ghi ra */
int srec_has_data(uint8_t type)
{
    return (type == 1 || type == 2 || type == 3);
}
