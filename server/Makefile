CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
TARGET  = server
SRCS    = main.c logger.c identity_client.c game.c protocol.c http_server.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Compilacion exitosa: ./$(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.log

# Ejemplo de ejecucion local (el servicio de identidad debe estar corriendo)
run: $(TARGET)
	IDENTITY_HOST=localhost IDENTITY_PORT=9090 ./$(TARGET) 8082 server.log
