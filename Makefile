
# set standard values, if not set by default
CXX ?= g++
CXXFLAGS ?= -O3

# project-specific flags
CXXFLAGS += -Wall $(DEBUG) $(shell gimptool-2.0 --cflags && pkg-config --cflags lensfun exiv2)
LDFLAGS += $(shell gimptool-2.0 --libs && pkg-config --libs lensfun exiv2)

# comment to disable OpenMP
CXXFLAGS += -fopenmp
LDFLAGS += -fopenmp

# project data
PLUGIN = gimp-lensfun
SOURCES = src/gimplensfun.cpp
HEADERS = src/LUT.hpp

# END CONFIG ##################################################################

.PHONY: all install userinstall clean uninstall useruninstall

all: $(PLUGIN)

OBJECTS = $(subst .cpp,.o,$(SOURCES))

$(PLUGIN): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $@ $*.c

install: $(PLUGIN)
	@gimptool-2.0 --install-admin-bin $^

userinstall: $(PLUGIN)
	@gimptool-2.0 --install-bin $^

uninstall:
	@gimptool-2.0 --uninstall-admin-bin $(PLUGIN)

useruninstall:
	@gimptool-2.0 --uninstall-bin $(PLUGIN)

clean:
	rm -f src/*.o $(PLUGIN)

debug:
	$(MAKE) $(MAKEFILE) DEBUG="-g -g3 -gdwarf-2 -D DEBUG"
