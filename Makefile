
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -Iserver/include
LDLIBS = -lsqlite3 -lcjson -largon2

TARGET = server/server
SRC = $(shell find server/src -name '*.c')

.PHONY: all run clean init-db

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDLIBS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

init-db:
	sqlite3 database/app.db < database/schema.sql