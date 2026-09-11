# Motorola S-record Parser

A small C program that reads a Motorola S-record (`.srec`, `.s19`, `.mot`) file,
verifies the checksum of every line, and writes the address + data of each data
record into a plain text file.

```
srec-parse.exe <input.srec> [output.txt]
```

---

## Table of contents

1. [What is an S-record?](#1-what-is-an-s-record)
2. [Anatomy of one line](#2-anatomy-of-one-line)
3. [The checksum, worked out by hand](#3-the-checksum-worked-out-by-hand)
4. [Project layout](#4-project-layout)
5. [Building](#5-building)
6. [Running it](#6-running-it)
7. [Code walkthrough: `srec.h`](#7-code-walkthrough-srech)
8. [Code walkthrough: `srec.c`](#8-code-walkthrough-srecc)
9. [Code walkthrough: `main.c`](#9-code-walkthrough-mainc)
10. [Design decisions](#10-design-decisions)
11. [Error codes](#11-error-codes)
12. [Things to try](#12-things-to-try)

---

## 1. What is an S-record?

When you compile firmware for a microcontroller you get a binary: raw bytes that
belong at specific addresses in flash memory. Binaries are awkward to send over a
serial cable or paste into an email — a stray byte can silently corrupt them and
nothing will tell you.

Motorola's answer (from the 1970s, still everywhere today) was to encode that
binary as **plain text**. Every byte becomes two ASCII hex characters, each chunk
of bytes gets tagged with the address it belongs at, and every line carries a
checksum so a corrupted line can be detected instead of programmed into a chip.

A real file looks like this:

```
S00F000068656C6C6F202020202000003C
S11F00007C0802A6900100049421FFF07C6C1B787C8C23783C6000003863000026
S11F001C4BFFFFE5398000007D83637880010014382100107C0803A64E800020E9
S111003848656C6C6F20776F726C642E0A0042
S5030003F9
S9030000FC
```

This is the key point for the parser: **the file contains the characters `'A'`
and `'F'`, not the byte `0xAF`.** Parsing means converting those character pairs
back into real bytes.

### Record types

The digit after the `S` says what the line is for, and — importantly — how many
bytes the address field uses:

| Type | Purpose | Address size |
|------|---------|--------------|
| `S0` | Header (file name, version — not program data) | 2 bytes |
| `S1` | Data, 16-bit address | 2 bytes |
| `S2` | Data, 24-bit address | 3 bytes |
| `S3` | Data, 32-bit address | 4 bytes |
| `S4` | Reserved, does not exist | — |
| `S5` | Count of S1/S2/S3 records that came before | 2 bytes |
| `S6` | Same, but a 24-bit count | 3 bytes |
| `S7` | End of file, 32-bit start address | 4 bytes |
| `S8` | End of file, 24-bit start address | 3 bytes |
| `S9` | End of file, 16-bit start address | 2 bytes |

Only **S1, S2 and S3** carry program data. Everything else is bookkeeping —
this program still checksum-verifies those lines, but does not write them to the
output file.

---

## 2. Anatomy of one line

Take line 4 of the sample above:

```
S 1 11 0038 48656C6C6F20776F726C642E0A00 42
│ │ │  │    │                            │
│ │ │  │    │                            └── (6) Checksum   : 1 byte
│ │ │  │    └─────────────────────────────── (5) Data       : 14 bytes
│ │ │  └──────────────────────────────────── (4) Address    : 2 bytes (because it is an S1)
│ │ └─────────────────────────────────────── (3) Byte Count : 0x11 = 17
│ └───────────────────────────────────────── (2) Type       : 1
└─────────────────────────────────────────── (1) Start      : 'S'
```

Because every field has a fixed size, the parser never has to search through the
line — it can index straight to each field:

| Field | Character offset | Length in characters |
|-------|------------------|----------------------|
| Start | 0 | 1 |
| Type | 1 | 1 |
| Byte Count | 2 | 2 |
| Address | 4 | `addr_len * 2` |
| Data | `4 + addr_len * 2` | `data_len * 2` |
| Checksum | `strlen - 2` (the last 2 chars) | 2 |

Two numbers unlock the whole line:

**Type → address width.** An `S1` has a 2-byte address, an `S3` has 4. You cannot
know where the data begins until you have read the type.

**Byte Count → data length.** Byte Count is *"how many bytes follow this field"*,
and those bytes are `address + data + checksum`. Rearranging:

```
data_len = byte_count - addr_len - 1
```

For our line: `17 - 2 - 1 = 14` bytes of data. And since each byte is 2
characters, the total line length must be exactly:

```
line_length = 4 + byte_count * 2        (1 for 'S', 1 for type, 2 for the count)
            = 4 + 17 * 2 = 38 characters
```

Count the characters in `S111003848656C6C6F20776F726C642E0A0042` — 38. That
formula is also a free integrity check: if the line is truncated or has junk
appended, the length will not match and the parser rejects it before it ever
looks at the checksum.

---

## 3. The checksum, worked out by hand

The rule: **sum the Byte Count, the Address bytes and the Data bytes; keep only
the lowest byte of that sum; then invert it.**

Note what is *not* included: the `S`, the type digit, and the checksum itself.

Using the same line, `S111003848656C6C6F20776F726C642E0A0042`:

```
byte count            0x11  =   17
address byte 1        0x00  =    0
address byte 2        0x38  =   56
data 'H'              0x48  =   72
data 'e'              0x65  =  101
data 'l'              0x6C  =  108
data 'l'              0x6C  =  108
data 'o'              0x6F  =  111
data ' '              0x20  =   32
data 'w'              0x77  =  119
data 'o'              0x6F  =  111
data 'r'              0x72  =  114
data 'l'              0x6C  =  108
data 'd'              0x64  =  100
data '.'              0x2E  =   46
data '\n'             0x0A  =   10
data '\0'             0x00  =    0
                            ------
total                        1213   = 0x4BD
```

Keep the low byte: `0x4BD & 0xFF = 0xBD`.
Invert it: `0xFF - 0xBD = 0x42`.

The line ends in `42`. It matches, so line 4 passes. (And the data spells
`Hello world.` — that is what the ASCII bytes decode to.)

Why `0xFF - x` instead of `~x`? For a value already masked to 8 bits they give
the identical result, but `0xFF - x` cannot accidentally produce a negative int
through C's integer promotion rules. It is the safer way to write it.

---

## 4. Project layout

```
Motorola S-record Parser/
├── srec.h          interface: error codes, the record struct, prototypes
├── srec.c          all parsing logic — no printf, no file I/O
├── main.c          command line, file loop, output file
├── Makefile        build rules
├── sample.srec     a valid file
├── bad.srec        same file with one checksum deliberately broken
└── README.md       this file
```

The split between `srec.c` and `main.c` is deliberate and is the most important
structural idea in the project:

- **`srec.c` never prints anything and never touches a file.** It takes a string,
  fills in a struct, and returns a status code. You could drop it into a bootloader
  running on a microcontroller with no `stdio` at all.
- **`main.c` owns all the I/O decisions** — what to print, where to write, when to
  give up. If you later wanted a GUI or a different output format, this is the only
  file you would rewrite.

---

## 5. Building

With `make`:

```bash
make            # produces srec-parse.exe
make clean      # removes the .o files and the exe
```

Or directly with gcc:

```bash
gcc -Wall -Wextra -std=c99 -O2 -o srec-parse.exe main.c srec.c
```

`-Wall -Wextra` turn on the warnings that catch real bugs — the code compiles
clean with both, and it is worth keeping it that way as you modify it.

---

## 6. Running it

**A valid file:**

```
> .\srec-parse.exe sample.srec
Line1: passed
Line2: passed
Line3: passed
Line4: passed
Line5: passed
Line6: passed
END
Output written to: sample.srec.out.txt
```

`sample.srec.out.txt` then contains one line per data record — address, a space,
then the data bytes as hex:

```
0x0000 7C0802A6900100049421FFF07C6C1B787C8C23783C60000038630000
0x001c 4BFFFFE5398000007D83637880010014382100107C0803A64E800020
0x0038 48656C6C6F20776F726C642E0A00
```

Six lines went in, three came out: the `S0` header, the `S5` count and the `S9`
terminator all passed their checksums but carry no program data.

**A file with a bad checksum:**

```
> .\srec-parse.exe bad.srec
Line1: passed
Line2: passed
ERROR: Line3: Checksum invalid!
END!
```

The program stops immediately — line 4 onward is never examined — and the output
file it had already started writing is deleted. It also returns exit code `1`, so
a script or batch file can detect the failure with `if errorlevel 1`.

**Choosing the output name:**

```
> .\srec-parse.exe sample.srec result.txt
```

Without a second argument the output name is the input name plus `.out.txt`.

---

## 7. Code walkthrough: `srec.h`

This header is the contract between the two `.c` files. Three things live here.

### The size limits

```c
#define SREC_MAX_DATA   250
#define SREC_MAX_LINE   (4 + 255 * 2 + 8)
```

These are not arbitrary. Byte Count is a single byte, so at most `0xFF = 255`
bytes can follow it. Subtract the largest possible address (4 bytes) and the
checksum (1 byte) and you are left with **250** data bytes — that is the biggest
a data field can ever legally be.

The line limit follows the same logic: `4` header characters plus `255 * 2`
payload characters is 514, and the extra 8 leaves room for `\r`, `\n` and the
terminating `\0`. Deriving the constants from the format instead of picking a
round number like 512 means the buffer cannot be too small by accident.

### The status enum

```c
typedef enum {
    SREC_OK = 0,
    SREC_ERR_NULL,
    ...
} srec_status_t;
```

Eleven distinct codes instead of a bare `0`/`-1`. This is what the requirement
*"functions must return detailed error codes"* is about: when something fails,
the caller learns **which** thing failed, and `srec_strerror()` can turn that into
a message a human can act on. `SREC_OK = 0` is pinned to zero so `if (st != SREC_OK)`
reads naturally.

### The record struct

```c
typedef struct {
    uint8_t  type;
    uint8_t  byte_count;
    uint32_t address;
    uint8_t  addr_len;
    uint8_t  data[SREC_MAX_DATA];
    uint8_t  data_len;
    uint8_t  checksum;
} srec_record_t;
```

One parsed line, in numeric form. Two details worth noticing:

- `address` is a `uint32_t` because an S3 address is 32 bits wide, while
  `addr_len` records how many of those bytes were actually present. You need both:
  the value for arithmetic, the width for printing and for the checksum.
- `data` is a **fixed array inside the struct**, not a pointer. No `malloc`, no
  `free`, no leak, no dangling pointer. It costs 250 bytes per record, which is
  nothing, and it makes the struct trivially safe to copy or put on the stack.
- The array is stored as **real bytes**, not the original hex text. The text is a
  transport encoding; once decoded we work with values.

The `uint8_t` / `uint32_t` types come from `<stdint.h>`. Prefer them over `int`
or `char` in this kind of code — they say exactly how many bits you get, on every
compiler and platform.

---

## 8. Code walkthrough: `srec.c`

### The offsets

```c
#define OFF_START   0
#define OFF_TYPE    1
#define OFF_COUNT   2
#define OFF_ADDR    4
```

Named constants instead of magic numbers scattered through the functions. The
data offset is not here because it depends on `addr_len`, so it is computed where
it is needed.

### `hex_value()` — one character to a number

```c
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
```

In ASCII the digits `'0'`–`'9'` sit next to each other, so `c - '0'` gives the
digit's value: `'7' - '0'` is `55 - 48 = 7`. Letters work the same way, offset by
10 because `'A'` represents 10. Returning `-1` for anything else is what lets the
caller detect garbage — and note the return type is `int`, not `uint8_t`,
precisely so `-1` is representable.

`static` means this function is private to `srec.c`. It is an implementation
detail, not part of the interface, so it does not belong in the header.

### `hex_to_byte()` — two characters to one byte

```c
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
```

`hi << 4` shifts the high digit left by 4 bits, which is the same as multiplying
by 16; `| lo` drops the low digit into the bits that just opened up:

```
hi = 0xA = 1010          lo = 0xF = 1111
hi << 4  = 1010 0000
        |  0000 1111
        = 1010 1111 = 0xAF
```

**This one function is the heart of the whole program.** Every other parser —
byte count, address, data, checksum — is just a loop around it. Because hex
validation happens in exactly one place, there is exactly one place a bad
character can slip through, and one place to fix if it ever did.

The function returns the *status* and delivers the *value* through a pointer.
That pattern repeats throughout the file: the return value is always "did it
work", never data. It is what makes detailed error codes possible.

### `addr_len_of_type()` — the type table

```c
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
```

The table from section 1, in code. The `default` branch catches `S4`, which the
standard reserves and never defines — so it is a genuine error, not an
oversight.

### `srec_check_start()` — component 1

Rejects `NULL`, rejects an empty string, and requires the first character to be
`S`. Lower-case `s` is accepted because some older tools emit it and there is no
ambiguity in doing so.

Note the order of the checks: `NULL` first, then empty, then wrong. You cannot
dereference a pointer before you have confirmed it is not `NULL`, and each check
makes the next one safe. That ordering discipline runs through every function
here.

### `srec_parse_type()` — component 2

```c
c = line[OFF_TYPE];
if (c < '0' || c > '9') return SREC_ERR_BAD_TYPE;

*type = (uint8_t)(c - '0');

return addr_len_of_type(*type, &dummy);
```

Converts the character to a number, then calls `addr_len_of_type()` purely to
validate it — the width goes into a throwaway variable. This avoids writing the
list of legal types twice, which would eventually drift out of sync. The type
table stays the single source of truth.

### `srec_parse_byte_count()` — component 3

```c
if (strlen(line) < OFF_COUNT + 2) return SREC_ERR_TOO_SHORT;
return hex_to_byte(&line[OFF_COUNT], byte_count);
```

Check the length before reading, then read. `&line[OFF_COUNT]` is a pointer to
character 2 of the line — that is how you hand a *position inside* a string to a
function that expects a string pointer.

### `srec_parse_address()` — component 4

```c
for (i = 0; i < len; i++) {
    st = hex_to_byte(&line[OFF_ADDR + i * 2], &b);
    if (st != SREC_OK) return st;
    value = (value << 8) | b;
}
```

S-records are **big-endian**: the first byte you read is the most significant.
`value = (value << 8) | b` is the standard idiom for assembling that — shift what
you have up by one byte and drop the new byte into the bottom. Watch it build
`0xAF25`:

```
start:          value = 0x0000
read 0xAF:      value = (0x0000 << 8) | 0xAF = 0x00AF
read 0x25:      value = (0x00AF << 8) | 0x25 = 0xAF25
```

The same three lines handle 2-, 3- and 4-byte addresses — only the loop count
changes.

### `srec_parse_data()` — component 5

```c
if (byte_count < addr_len + 1) return SREC_ERR_COUNT_TOO_SMALL;
n = (uint8_t)(byte_count - addr_len - 1);
```

The subtraction from section 2. The guard above it matters more than it looks:
these are **unsigned** types, so if `byte_count` were smaller than `addr_len + 1`
the subtraction would not go negative — it would wrap around to a huge positive
number and the loop below would read far past the end of the line. Checking
first is what prevents that. Unsigned underflow is one of the classic C bugs, and
this is exactly the shape it takes.

```c
offset = (size_t)OFF_ADDR + addr_len * 2;
if (strlen(line) < offset + (size_t)n * 2) return SREC_ERR_TOO_SHORT;
```

Then confirm the characters actually exist before reading them. Only after both
guards pass does the loop run.

### `srec_parse_checksum()` — component 6

```c
expected_len = 4 + (size_t)byte_count * 2;
if (strlen(line) != expected_len) return SREC_ERR_BAD_LENGTH;

return hex_to_byte(&line[expected_len - 2], checksum);
```

This function does double duty. The checksum is the last two characters, so to
find it you must know where the line ends — and checking that the line is
*exactly* the promised length catches truncation, padding, and stray characters
in one comparison. `!=` rather than `<` is the point: a line that is too long is
just as broken as one that is too short.

### `srec_verify_checksum()`

```c
sum += rec->byte_count;

for (i = 0; i < rec->addr_len; i++) {
    sum += (rec->address >> (8 * (rec->addr_len - 1 - i))) & 0xFF;
}

for (i = 0; i < rec->data_len; i++) {
    sum += rec->data[i];
}

computed = (uint8_t)(0xFF - (sum & 0xFF));
```

The address was assembled into a single `uint32_t`, so to add its bytes to the
sum we have to take it apart again. Shift the byte you want down to the bottom,
then mask off everything above it. For a 2-byte address:

```
i = 0:  address >> 8   then & 0xFF  ->  the high byte
i = 1:  address >> 0   then & 0xFF  ->  the low byte
```

`sum` is a `uint32_t` even though only the low byte survives. It could be a
`uint8_t` and still produce the correct answer through natural wraparound, but
relying on overflow to be correct is the kind of cleverness that confuses the
next reader — including you in six months. Let it grow, mask at the end.

### `srec_parse_line()` — the orchestrator

```c
st = srec_check_start(line);
if (st != SREC_OK) return st;

st = srec_parse_type(line, &rec->type);
if (st != SREC_OK) return st;
...
```

Calls the six component functions in order and **returns on the first failure**.
The order is not cosmetic — it is a dependency chain:

```
start ──> type ──> byte count ──> address ──> data ──> checksum field ──> verify
                                     │           │
                          type gives addr_len    │
                                 byte count + addr_len give data_len
```

You cannot find the data until you know the address width, and you cannot know
the address width until you have read the type. Failing fast also means a garbage
line never reaches code that would index into it.

The `memset(rec, 0, sizeof(*rec))` at the top clears the struct so a caller who
ignores the error code still sees zeros rather than whatever was on the stack.

### `srec_strerror()` and `srec_has_data()`

`srec_strerror()` maps a code to a message — the one place in `srec.c` that knows
about human-readable text, and even that returns a string rather than printing
it. `srec_has_data()` answers "should this record go into the output file", true
only for S1/S2/S3.

---

## 9. Code walkthrough: `main.c`

### `trim()`

```c
while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                   s[len - 1] == ' '  || s[len - 1] == '\t')) {
    s[--len] = '\0';
}
```

`fgets()` keeps the newline character it read, and files written on Windows carry
a `\r` before it. Since the checksum is located as *"the last two characters"*, a
leftover `\r\n` would put the parser two characters off and every single line
would fail. This is the most common bug in beginner S-record parsers.

Leading whitespace is stripped too — with `memmove()` rather than `strcpy()`,
because the source and destination overlap and `strcpy()` is undefined behaviour
in that case. The `+ 1` in `memmove(s, s + start, len - start + 1)` copies the
terminating `\0` along with the text.

### Working out the output path

```c
if (argc == 3) {
    strcpy(out_path, argv[2]);
} else if (make_output_path(argv[1], out_path, sizeof(out_path)) != 0) {
    ...
}
```

Two argument, use the default name; three arguments, use the one given. Both
paths check the length against `sizeof(out_path)` **before** copying — `strcpy()`
and `strcat()` will happily run off the end of a buffer, so the caller has to do
the checking. Passing `sizeof(out_path)` into the helper rather than hard-coding
1024 inside it means the check stays correct if the buffer is ever resized.

### The main loop

```c
while (fgets(line, sizeof(line), fin) != NULL) {
    line_no++;
    trim(line);

    if (line[0] == '\0') {
        continue;
    }
    ...
}
```

`fgets()` is the right tool here: it takes the buffer size and will not overflow
it, unlike `gets()` (which is so unsafe it was removed from the C standard) or a
bare `scanf("%s")`.

Blank lines are skipped **after** `line_no++`, so the number in `Line5: passed`
always matches the line number your editor shows — which is the whole point of
printing it.

### Success and failure

```c
if (st != SREC_OK) {
    printf("ERROR: Line%lu: %s\n", line_no, srec_strerror(st));
    printf("END!\n");

    fclose(fin);
    fclose(fout);
    remove(out_path);
    return EXIT_FAILURE;
}

printf("Line%lu: passed\n", line_no);
```

Both files are closed before `remove()`. On Windows you cannot delete a file that
still has an open handle, so the order is not optional. `EXIT_FAILURE` gives the
shell a non-zero exit code.

### Writing a data record

```c
fprintf(fout, "0x%0*x ", rec.addr_len * 2, rec.address);
for (i = 0; i < rec.data_len; i++) {
    fprintf(fout, "%02X", rec.data[i]);
}
fprintf(fout, "\n");
```

`%0*x` is worth learning: the `*` means *"take the field width from the next
argument"*. Passing `rec.addr_len * 2` makes an S1 print as `0x0038` and an S3 as
`0x00000038`, with no `if` statement — the address is shown at its true width.

Lower-case `x` for the address and upper-case `X` for the data simply matches the
format in the requirement.

---

## 10. Design decisions

**Why stop at the first bad line?** The requirement asks for it, and it matches
how these files are used: an S-record describes memory to be programmed into a
chip. If one line is corrupt you cannot trust any of it, so there is nothing to
gain from parsing the rest.

**Why write as we go, and delete on error?** The requirement offered three
strategies:

1. Parse everything into memory, then write the file at the end.
2. Accumulate the output in a buffer through a pointer, then dump it.
3. Create the file empty, append as you parse, delete it if anything fails.

This project uses **(3)**. Options 1 and 2 both need the entire file in RAM, which
means `malloc`, a growth strategy when the buffer fills, and a `free` on every
exit path — a lot of machinery, and a lot of ways to leak. Streaming needs one
522-byte line buffer no matter whether the input is 6 lines or 600,000.

The trade-off is that a partial file exists on disk while the program runs. The
`remove()` on the error path is what makes that acceptable: when the program
finishes you either have a complete, fully verified output file or no file at
all. There is no third state where a half-written file looks finished.

**Why no dynamic memory anywhere?** For a program this size `malloc` would add
failure cases without buying anything. The maximum sizes are fixed by the file
format itself, so fixed buffers are provably large enough. This is also standard
practice in embedded work, where dynamic allocation is often banned outright.

**Why does `srec.c` never print?** A library that prints has decided how it will
be used. Returning a status code lets the caller print it, log it, show it in a
dialog, or count errors silently — and it means `srec.c` compiles on a target
with no `stdio` at all.

---

## 11. Error codes

Every function in `srec.c` returns one of these:

| Code | Meaning | Typical cause |
|------|---------|---------------|
| `SREC_OK` | Success | — |
| `SREC_ERR_NULL` | A `NULL` pointer was passed in | Programming mistake in the caller |
| `SREC_ERR_EMPTY` | Line is empty | Blank line (`main.c` filters these first) |
| `SREC_ERR_NO_START` | Line does not begin with `S` | Not an S-record file, or a stray comment |
| `SREC_ERR_BAD_TYPE` | Type is not a digit, or is `S4` | Corrupted line |
| `SREC_ERR_BAD_HEX` | Non-hex character in a hex field | Corruption, or the file passed through a text filter |
| `SREC_ERR_TOO_SHORT` | Line ends before a field does | Truncated line |
| `SREC_ERR_BAD_LENGTH` | Length does not match Byte Count | Truncated or padded line |
| `SREC_ERR_COUNT_TOO_SMALL` | Byte Count cannot hold address + checksum | Corrupted Byte Count field |
| `SREC_ERR_DATA_TOO_LONG` | Data exceeds `SREC_MAX_DATA` | Corrupted Byte Count field |
| `SREC_ERR_CHECKSUM` | Computed checksum ≠ the one in the line | A byte somewhere in the line changed |

Note that a single corrupted character usually triggers `SREC_ERR_CHECKSUM`,
while a corrupted *Byte Count* tends to trigger a length or range error first —
which is more useful, because it tells you the line is malformed rather than just
"wrong somewhere".

---

## 12. Things to try

Small exercises, roughly in order of difficulty:

1. **Break a byte on purpose.** Change one hex character in the middle of a data
   field in `sample.srec` and confirm you get `Checksum invalid!` on that line.
2. **Break the Byte Count instead** and notice you get `Line length does not
   match byte count` — a different, more specific error.
3. **Decode the data by hand.** `48656C6C6F` is `Hello`. Look up the rest of
   line 4 in an ASCII table.
4. **Add a `-v` flag** to `main.c` that also prints the type, byte count and
   address of every line. Note that this needs no change to `srec.c` at all —
   the struct already holds everything.
5. **Verify the S5 record.** It claims how many data records precede it. Count
   the S1/S2/S3 records as you go and compare. (`S5030003` says 3 — and there
   are 3.)
6. **Merge adjacent records.** If one record ends exactly where the next begins,
   they could be written as a single output line. This needs you to buffer, which
   is where output strategy 1 or 2 starts to earn its keep.
