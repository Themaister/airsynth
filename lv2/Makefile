BUNDLE := airsynth.lv2
INSTALL_DIR = /usr/lib/lv2

SOURCE := airsynth.cpp ../synth.cpp ../noiseiir.cpp
OBJECTS := $(SOURCE:.cpp=.o)

LDFLAGS += -fPIC $(shell pkg-config lv2-plugin --libs) -shared
CXXFLAGS += -fPIC $(shell pkg-config lv2-plugin --cflags) -std=gnu++11 -Wall -pedantic

ifeq ($(DEBUG), 1)
   CXXFLAGS += -O0 -g
else
   CXXFLAGS += -O2
endif

all: $(BUNDLE)

$(BUNDLE): manifest.ttl airsynth.ttl airsynth.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	install -m644 manifest.ttl airsynth.ttl airsynth.so $(BUNDLE)

airsynth.peg: airsynth.ttl
	lv2peg $< $@

airsynth.so: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp airsynth.peg
	$(CXX) -c -o $@ $< $(CXXFLAGS)

install:
	cp -r $(BUNDLE) $(INSTALL_DIR)

clean:
	rm -rf $(BUNDLE) airsynth.so $(OBJECTS) *.peg

.PHONY: clean

