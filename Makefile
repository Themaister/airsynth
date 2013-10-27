TARGET := midi

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:.cpp=.o)

CXXFLAGS += -O0 -g -Wall -pedantic -std=gnu++11 -pedantic $(shell pkg-config alsa --cflags)
LDFLAGS += $(shell pkg-config alsa --libs) -lm

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

