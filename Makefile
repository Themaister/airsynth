TARGET := midi

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:.cpp=.o)

CXXFLAGS += -Wall -pedantic -std=gnu++11 -pedantic $(shell pkg-config alsa --cflags)
LDFLAGS += $(shell pkg-config alsa --libs) -lm

ifeq ($(DEBUG), 1)
   CXXFLAGS += -O0 -g
else
   CXXFLAGS += -O3 -ffast-math -march=native
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(OBJECTS)

.PHONY: clean

