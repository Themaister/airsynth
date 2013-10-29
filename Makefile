TARGET := airsynth

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
HEADERS := $(wildcard *.hpp)

CXXFLAGS += -Wall -pedantic -std=gnu++11 -pedantic $(shell pkg-config alsa sndfile --cflags)
LDFLAGS += $(shell pkg-config alsa sndfile --libs) -lm

ifeq ($(DEBUG), 1)
   CXXFLAGS += -O0 -g
else
   CXXFLAGS += -O3 -ffast-math -march=native
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(HEADERS)
	$(CXX) -o $@ -c $< $(CXXFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(OBJECTS)

.PHONY: clean

