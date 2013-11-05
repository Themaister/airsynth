TARGET := airsynth

SOURCES := $(wildcard *.cpp)
CSOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.cpp=.o) $(CSOURCES:.c=.o)
HEADERS := $(wildcard *.hpp)

CXXFLAGS += -Wall -pedantic -std=gnu++11 -pedantic $(shell pkg-config jack sndfile --cflags) -DBLIPPER_FIXED_POINT=0
CFLAGS += -ansi -pedantic -Wall -DBLIPPER_FIXED_POINT=0
LDFLAGS += $(shell pkg-config jack sndfile --libs) -lm

ifeq ($(DEBUG), 1)
   CFLAGS += -O0 -g
   CXXFLAGS += -O0 -g
else
   CXXFLAGS += -O3 -ffast-math -march=native
   CFLAGS += -O3 -ffast-math -march=native
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(HEADERS)
	$(CXX) -o $@ -c $< $(CXXFLAGS)

%.o: %.c $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(OBJECTS)

.PHONY: clean

