# Copyright (c) 2020 Mark Polyakov, Karen Haining, Muki Kiboigo, Lucius Carr
# (If you edit the file, add your name here!)

# ------------------------------------------------------
# Source discovery
# ------------------------------------------------------

# Everything is unified in src/ now
SRCS := $(wildcard src/*.cpp)
TESTS := $(wildcard test/*.cpp)

# Documentation setup
MANS := $(wildcard documentation/*.man)
MAN_TXTS := $(patsubst documentation/%.man, documentation/%.txt, $(MANS))
MAN_HS := $(patsubst documentation/%.man, documentation/man-%.h, $(MANS))
DOXYGEN_DIR := ./documentation/doxygen

OBJS := $(patsubst %.cpp,%.o,$(SRCS))
# Test objects include everything EXCEPT the main entry point to avoid double main()
TEST_OBJS := $(patsubst %.cpp,%.o,$(TESTS) $(filter-out %/main.o, $(OBJS)))

DEPS := $(patsubst %.cpp,%.d,$(SRCS) $(TESTS))

BIN      := lost
TEST_BIN := ./lost-test
BSC      := bright-star-catalog.tsv

# ------------------------------------------------------
# Libraries and compiler flags
# ------------------------------------------------------

# SFML and Cairo are now core dependencies for all targets
SFML_LIBS := -lsfml-graphics -lsfml-window -lsfml-system
LIBS      := -lcairo $(SFML_LIBS)

CXXFLAGS += -Ivendor -Isrc -Idocumentation -Wall -Wextra -Wno-missing-field-initializers -pedantic --std=c++11

# --- macOS/Homebrew Path Auto-Detection ---
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    HOMEBREW_PREFIX := $(shell [ -d /opt/homebrew ] && echo /opt/homebrew || echo /usr/local)
    CXXFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDFLAGS  += -L$(HOMEBREW_PREFIX)/lib
endif

RELEASE_CXXFLAGS := $(CXXFLAGS) -O3
CXXFLAGS += -ggdb -fno-omit-frame-pointer

ifndef LOST_DISABLE_ASAN
    CXXFLAGS += -fsanitize=address
    LDFLAGS  += -fsanitize=address
endif

RELEASE_LDFLAGS := $(LDFLAGS)

ifdef LOST_FLOAT_MODE
    CXXFLAGS += -Wdouble-promotion -Werror=double-promotion -D LOST_FLOAT_MODE
endif

# ------------------------------------------------------
# Primary build rules
# ------------------------------------------------------

all: $(BIN) $(BSC)

release: CXXFLAGS := $(RELEASE_CXXFLAGS)
release: LDFLAGS := $(RELEASE_LDFLAGS)
release: all

# Main program
$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBS)

# Manpage conversion rules
documentation/%.txt: documentation/%.man
	groff -mandoc -Tascii $< > $@
	printf '\0' >> $@

documentation/man-%.h: documentation/%.txt
	xxd -i $< > $@

# Header Generation dependency: Ensures manpage headers exist before any .cpp file compiles
$(OBJS): $(MAN_HS)
$(TEST_OBJS): $(MAN_HS)

docs:
	doxygen

lint:
	cpplint --recursive src test

# Generic object compilation rule
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

-include $(DEPS)

# Testing
test: $(BIN) $(BSC) $(TEST_BIN)
	$(TEST_BIN)
	bash ./test/scripts/readme-examples-test.sh
	bash ./test/scripts/random-crap.sh

$(TEST_BIN): $(TEST_OBJS)
	$(CXX) $(LDFLAGS) -o $(TEST_BIN) $(TEST_OBJS) $(LIBS)

# Cleaning
clean:
	rm -f $(OBJS) $(DEPS) $(TEST_OBJS) $(MAN_HS) $(MAN_TXTS)
	rm -rf $(DOXYGEN_DIR)

clean_all: clean
	rm -f $(BSC) $(BIN) $(TEST_BIN)

.PHONY: all clean test docs lint release clean_all