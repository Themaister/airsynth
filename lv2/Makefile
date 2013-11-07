BUNDLE := airsynth.lv2
INSTALL_DIR = /usr/lib/lv2

SOURCE := airsynth.cpp ../synth.cpp ../noiseiir.cpp ../sawtooth.cpp ../square.cpp
CSOURCE := ../blipper.c
OBJECTS := $(SOURCE:.cpp=.o) $(CSOURCE:.c=.o)
TTL_FILES := $(wildcard *.ttl)

LDFLAGS += -fPIC $(shell pkg-config lv2-plugin --libs) -shared -Wl,-no-undefined
CXXFLAGS += -fPIC $(shell pkg-config lv2-plugin --cflags) -std=gnu++11 -Wall -pedantic -DBLIPPER_FIXED_POINT=0
CFLAGS += -fPIC -ansi -Wall -pedantic -DBLIPPER_FIXED_POINT=0

ifeq ($(DEBUG), 1)
   CXXFLAGS += -O0 -g
   CFLAGS += -O0 -g
else
   CXXFLAGS += -O2 -ffast-math -march=native
   CFLAGS += -O2 -ffast-math -march=native
endif

all: $(BUNDLE)

$(BUNDLE): $(TTL_FILES) airsynth.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	install -m644 $(TTL_FILES) airsynth.so $(BUNDLE)

noise.peg: noise.ttl
	lv2peg $< $@

airsynth.so: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp noise.peg
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

install:
	cp -r $(BUNDLE) $(INSTALL_DIR)

clean:
	rm -rf $(BUNDLE) airsynth.so $(OBJECTS) *.peg

.PHONY: clean

