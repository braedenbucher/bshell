EXEC = bshell
CC = gcc
CFLAGS = -g -Wall -Wextra
SRC = src/main.c
LIBS = -lreadline

build:
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(EXEC)

run: build
	./$(EXEC)

clean:
	rm -f $(EXEC)

.PHONY: build run clean