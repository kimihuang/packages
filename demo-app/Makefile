# Makefile for demo-app

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
TARGET = demo-app
SRC = demo-app.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

install:
	install -m 0755 $(TARGET) $(DESTDIR)/usr/bin/
