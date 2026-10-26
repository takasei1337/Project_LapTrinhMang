CC = gcc
CFLAGS = -Wall -Wextra -g -Iserver/include

TARGET = server/server
SRC = $(wildcard server/src/*.c)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)