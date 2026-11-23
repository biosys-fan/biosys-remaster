# Biosys native port. Requires: clang++, SDL3 (brew), zlib, FreeType (brew; used by src/engine/font.cpp only: the TrueType
# glyphs of the GDI text the original game draws, see src/engine/font.h).
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -I/opt/homebrew/include -I/opt/homebrew/include/freetype2
LDFLAGS  ?= -L/opt/homebrew/lib
# what the tools link (engine core) and what the app adds; packaging/make_app.sh overrides both with its own builds
LIBS     ?= -lz -lfreetype
SDL_LIBS ?= -lSDL3
B        := build
# Development only: where the developer keeps the original discs and the analysis (the folder above this one:
# setup-cd/, game-cd/, analysis/). Tools and tests fall back to it; the released app is built with DEVFLAGS= .
DEVROOT  ?= $(abspath ..)
DEVFLAGS ?= -DBIOSYS_DEV_ROOT='"$(DEVROOT)"'

ENGINE_SRC := $(wildcard src/engine/*.cpp)
ENGINE_OBJ := $(ENGINE_SRC:src/%.cpp=$(B)/%.o)
GAME_SRC   := $(wildcard src/game/*.cpp)
GAME_OBJ   := $(GAME_SRC:src/%.cpp=$(B)/%.o)
APP_SRC    := $(wildcard src/app/*.cpp)
APP_OBJ    := $(APP_SRC:src/%.cpp=$(B)/%.o)
# first-run import of the player's own CD images (src/install: zlib and <filesystem> only, no SDL)
INSTALL_SRC := $(wildcard src/install/*.cpp)
INSTALL_OBJ := $(INSTALL_SRC:src/%.cpp=$(B)/%.o)

TOOL_SRC   := $(wildcard src/tools/*.cpp)
TOOLS      := $(TOOL_SRC:src/tools/%.cpp=$(B)/biosys-%)

all: $(TOOLS) $(B)/biosys

# Engine core: no SDL allowed in src/engine.
# Each src/tools/<name>.cpp becomes build/biosys-<name>, linked with the engine core only.
$(B)/biosys-%: $(ENGINE_OBJ) $(GAME_OBJ) $(INSTALL_OBJ) $(B)/tools/%.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

# canvastest needs the engine core only (no game logic, no SDL)
$(B)/biosys-canvastest: $(ENGINE_OBJ) $(B)/tools/canvastest.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(B)/biosys: $(ENGINE_OBJ) $(GAME_OBJ) $(INSTALL_OBJ) $(APP_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS) $(SDL_LIBS)

$(B)/%.o: src/%.cpp $(wildcard src/engine/*.h) $(wildcard src/game/*.h) $(wildcard src/app/*.h) $(wildcard src/install/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEVFLAGS) -c -o $@ $<

test: all
	bash tests/run.sh

clean:
	rm -rf $(B)

.SECONDARY:
.PHONY: all test clean
