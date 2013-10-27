TARGET := midi

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)

CFLAGS += -O0 -g -Wall -pedantic -std=gnu99 $(shell pkg-config alsa --cflags)
LDFLAGS += $(shell pkg-config alsa --libs)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

