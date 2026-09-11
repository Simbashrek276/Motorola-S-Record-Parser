CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2
TARGET  = srec-parse.exe
OBJS    = main.o srec.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

main.o: main.c srec.h
	$(CC) $(CFLAGS) -c main.c

srec.o: srec.c srec.h
	$(CC) $(CFLAGS) -c srec.c

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
