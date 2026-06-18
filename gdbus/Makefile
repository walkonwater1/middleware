CC = gcc
CFLAGS = -Wall -Wextra -g
LIBS = $(shell pkg-config --cflags --libs gio-2.0)

TARGETS = gdbus-demo-service gdbus-demo-client

.PHONY: all clean

all: $(TARGETS)

gdbus-demo-service: gdbus-demo-service.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

gdbus-demo-client: gdbus-demo-client.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGETS) *.o
