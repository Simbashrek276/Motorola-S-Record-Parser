/*
 * main.c
 *
 * Chạy  srec-parse.exe <file.srec> [ket_qua.txt]
 * Không ghi tham số thứ 2 thì tự thêm ".out.txt" vào sau tên file vào.
 *
 * Cách ghi kết quả (cách 3 của đề): tạo file rỗng trước, parse tới đâu ghi
 * tới đó, lỗi thì xoá luôn file. Chạy xong hoặc là có file đầy đủ, hoặc là
 * không có file nào, không bị dở dang.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "srec.h"

#define OUT_SUFFIX  ".out.txt"

/* fgets giữ lại luôn ký tự xuống dòng, file Windows còn dính thêm '\r'.
 * Mà checksum lại lấy theo "2 ký tự cuối" nên còn sót \r\n là sai hết cả file.
 * Chỗ này lúc đầu bị dính, mất khá lâu mới ra */
static void trim(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' '  || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }

    while (s[start] == ' ' || s[start] == '\t') {
        start++;
    }

    if (start > 0) {
        /* memmove chứ không strcpy, vì nguồn với đích đè lên nhau.
         * +1 để chép luôn '\0' */
        memmove(s, s + start, len - start + 1);
    }
}

/* tên file vào + ".out.txt". Tự check độ dài vì strcpy/strcat không check hộ */
static int make_output_path(const char *in_path, char *out_path, size_t size)
{
    if (strlen(in_path) + strlen(OUT_SUFFIX) + 1 > size) {
        return -1;
    }

    strcpy(out_path, in_path);
    strcat(out_path, OUT_SUFFIX);
    return 0;
}

int main(int argc, char *argv[])
{
    FILE *fin  = NULL;
    FILE *fout = NULL;
    char  line[SREC_MAX_LINE];
    char  out_path[1024];
    unsigned long line_no = 0;
    srec_record_t rec;
    srec_status_t st;

    /* argv[0] là tên chương trình, argv[1] mới là tham số đầu tiên */
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <input.srec> [output.txt]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 3) {
        if (strlen(argv[2]) >= sizeof(out_path)) {
            fprintf(stderr, "ERROR: Output path is too long\n");
            return EXIT_FAILURE;
        }
        strcpy(out_path, argv[2]);
    } else if (make_output_path(argv[1], out_path, sizeof(out_path)) != 0) {
        fprintf(stderr, "ERROR: Input path is too long\n");
        return EXIT_FAILURE;
    }

    fin = fopen(argv[1], "r");
    if (fin == NULL) {
        fprintf(stderr, "ERROR: Cannot open input file '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    fout = fopen(out_path, "w");        /* "w" tạo file mới rỗng */
    if (fout == NULL) {
        fprintf(stderr, "ERROR: Cannot create output file '%s'\n", out_path);
        fclose(fin);
        return EXIT_FAILURE;
    }

    /* fgets an toàn vì có truyền kích thước ô nhớ vào, không bị tràn như gets().
     * Hết file thì nó trả về NULL */
    while (fgets(line, sizeof(line), fin) != NULL) {
        line_no++;
        trim(line);

        /* bỏ qua dòng trống nhưng vẫn đếm, để số dòng in ra khớp với editor */
        if (line[0] == '\0') {
            continue;
        }

        st = srec_parse_line(line, &rec);

        if (st != SREC_OK) {
            printf("ERROR: Line%lu: %s\n", line_no, srec_strerror(st));
            printf("END!\n");

            /* đóng file xong mới xoá được, Windows không cho xoá file đang mở */
            fclose(fin);
            fclose(fout);
            remove(out_path);
            return EXIT_FAILURE;
        }

        printf("Line%lu: passed\n", line_no);

        if (srec_has_data(rec.type) && rec.data_len > 0) {
            uint8_t i;

            /* dấu * trong "%0*x" = lấy độ rộng từ tham số đứng trước, ở đây là
             * addr_len*2. Nhờ vậy S1 in ra 0x0038 còn S3 in ra 0x00000038,
             * khỏi phải viết if cho từng loại */
            fprintf(fout, "0x%0*x ", rec.addr_len * 2, rec.address);

            for (i = 0; i < rec.data_len; i++) {
                fprintf(fout, "%02X", rec.data[i]);
            }

            fprintf(fout, "\n");
        }
    }

    printf("END\n");

    fclose(fin);
    fclose(fout);

    printf("Output written to: %s\n", out_path);
    return EXIT_SUCCESS;
}
