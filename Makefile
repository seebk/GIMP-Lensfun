
# enable/disable debug mode
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CXXFLAGS += -DDEBUG=true
else
    CXXFLAGS += -DDEBUG=false
endif


# set standard values, if not set by default
CXX ?= g++
CXXFLAGS += -O3 -Wall


# project-specific flags
EXTRA_CXXFLAGS += $(shell gimptool-2.0 --cflags && pkg-config --cflags lensfun exiv2 gexiv2)
LDFLAGS += $(shell gimptool-2.0 --libs && pkg-config --libs lensfun exiv2 gexiv2) -lstdc++


# set some system dependent options
SYS := $(shell gcc -dumpmachine)
ifneq (, $(findstring mingw, $(SYS)))
	ifeq ($(DEBUG), 0)
		LDFLAGS += -Wl,-subsystem,windows
	endif
else
	# comment to disable OpenMP
	CXXFLAGS += -fopenmp
	LDFLAGS += -fopenmp
endif


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

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(EXTRA_CXXFLAGS) -c -o $@ $*.cpp

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
