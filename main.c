/*
 * main.c - srec-parse.exe <input.srec> [output.txt]
 *
 * The output file is created empty up front and appended to as we go. If any
 * line fails we delete it, so a run either produces a complete file or none.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "srec.h"

#define OUT_SUFFIX  ".out.txt"

/* fgets keeps the newline, and Windows files carry a '\r' too. Either one
 * would land inside the checksum field and break the fixed offsets. */
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
        memmove(s, s + start, len - start + 1);
    }
}

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

    fout = fopen(out_path, "w");
    if (fout == NULL) {
        fprintf(stderr, "ERROR: Cannot create output file '%s'\n", out_path);
        fclose(fin);
        return EXIT_FAILURE;
    }

    while (fgets(line, sizeof(line), fin) != NULL) {
        line_no++;
        trim(line);

        /* Skip blanks but keep counting, so Line<x> matches the editor. */
        if (line[0] == '\0') {
            continue;
        }

        st = srec_parse_line(line, &rec);

        if (st != SREC_OK) {
            printf("ERROR: Line%lu: %s\n", line_no, srec_strerror(st));
            printf("END!\n");

            fclose(fin);
            fclose(fout);
            remove(out_path);
            return EXIT_FAILURE;
        }

        printf("Line%lu: passed\n", line_no);

        if (srec_has_data(rec.type) && rec.data_len > 0) {
            uint8_t i;

            /* "%0*x" takes the field width from the argument before it, so an
             * S1 prints 0x0000 and an S3 prints 0x00000000. */
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
