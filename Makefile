# D2RCabbyCodes - CabbyCodes for Diablo II: Resurrected, built as a D2RLoader
# plugin (d2rl-cabbycodes.dll) and cross-compiled on Linux with mingw-w64 (no
# MSVC / no Windows needed).
#
# D2R.exe imports only Blizzard's protector DLL, so there is no game-shipped DLL
# to stand in for the way the RE/FEAR siblings do with steam_api. The community
# loader (D2RLoader, d2rloader.net) is what every other runtime mod uses; its
# plugin SDK (contrib/D2RLoader-PluginSDK, MIT) is header-only, and the loader
# tracks and chains every code hook, which is what keeps this mod and the others
# working side by side.

CXX := x86_64-w64-mingw32-g++
CC  := x86_64-w64-mingw32-gcc
RC  := x86_64-w64-mingw32-windres

PROJECT   := D2RCabbyCodes
PLUGIN_ID := cabbycodes
NAME      := d2rl-$(PLUGIN_ID)
# The mod folder the game is launched with (-mod <name>); D2RMM's output mod.
MOD_NAME  ?= D2RMM

# VERSION holds the single source of truth. Change it with `make version X.Y.Z`.
VERSION   := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
VER_PARTS := $(subst ., ,$(VERSION))
VER_MAJOR := $(word 1,$(VER_PARTS))
VER_MINOR := $(word 2,$(VER_PARTS))
VER_PATCH := $(word 3,$(VER_PARTS))
# DEV=1 (the default while developing) keeps the developer aids in: the image
# dump, the command file and the UI message log. Each flavour has its own
# build directory, so `make` and the release `make dist` never share objects.
DEV       ?= 1
BUILD     := build/$(if $(filter 1,$(DEV)),dev,release)
TARGET    := $(BUILD)/$(NAME).dll

# Per-machine config (game install path), kept out of git. Copy
# config.mk.example to config.mk and set GAME_DIR there. The path contains
# spaces, so it is only ever used quoted inside shell recipes below.
-include config.mk

ifneq (,$(filter version rev,$(MAKECMDGOALS)))
REV := $(strip $(filter-out version rev,$(MAKECMDGOALS)))
$(eval $(REV):;@:)
endif

SRCS := $(wildcard src/*.cpp)

# Dear ImGui (vendored, MIT): the core plus the Win32 and DX12 backends - D2R
# renders through Direct3D 12 only.
IMGUI_DIR  := contrib/imgui
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_dx12.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_win32.cpp

# MinHook (vendored, BSD-2): only for the DXGI Present/ResizeBuffers/
# CreateSwapChain entry hooks. Every hook into the game's own code goes through
# D2RLoader instead, so the loader can track it and other plugins can chain it.
MINHOOK_DIR  := contrib/minhook
MINHOOK_SRCS := $(MINHOOK_DIR)/src/buffer.c $(MINHOOK_DIR)/src/hook.c \
                $(MINHOOK_DIR)/src/trampoline.c $(MINHOOK_DIR)/src/hde/hde64.c

SDK_INC  := contrib/D2RLoader-PluginSDK/include
RES_SRCS := $(wildcard res/*.rc)

MOD_OBJS     := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))
IMGUI_OBJS   := $(patsubst $(IMGUI_DIR)/%.cpp,$(BUILD)/imgui/%.o,$(IMGUI_SRCS))
MINHOOK_OBJS := $(patsubst $(MINHOOK_DIR)/src/%.c,$(BUILD)/minhook/%.o,$(MINHOOK_SRCS))
RES_OBJS     := $(patsubst res/%.rc,$(BUILD)/res/%.o,$(RES_SRCS))
OBJS         := $(MOD_OBJS) $(IMGUI_OBJS) $(MINHOOK_OBJS) $(RES_OBJS)

DEVFLAGS := $(if $(filter 1,$(DEV)),-DD2RCC_DEV=1,)

# -MMD -MP: emit .d header-dependency files so editing a header recompiles every
# .cpp that includes it.
CXXFLAGS := $(DEVFLAGS) -std=c++20 -O2 -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers \
            -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD \
            -I$(IMGUI_DIR) -I$(SDK_INC) -I$(MINHOOK_DIR)/include \
            -ffunction-sections -fdata-sections \
            -MMD -MP
CFLAGS   := -O2 -Wall -DWIN32_LEAN_AND_MEAN -I$(MINHOOK_DIR)/include \
            -ffunction-sections -fdata-sections -MMD -MP
RCFLAGS  := -I res -I $(BUILD)/res -I $(SDK_INC)

# -static*: fold the C/C++ runtime in so the DLL has no external mingw runtime
# deps. Only the three D2RLoader entry points are exported (dllexport in
# src/plugin.cpp); --exclude-all-symbols keeps everything else private.
LDFLAGS  := -shared \
            -static -static-libgcc -static-libstdc++ \
            -Wl,--gc-sections -Wl,--exclude-all-symbols

# Only DLLs every Windows (and Wine) has are imported. d3d12 and dxgi are looked
# up at run time (they are already in the process when the overlay starts).
LDLIBS   := -luser32 -lshell32 -lole32 -lgdi32 -ldwmapi -ldxguid -luuid -lcomctl32

.PHONY: all clean install install-mod install-d2rmm uninstall dist package rev version test seedfinder seedmodel-test
all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
	@echo ">> Built $@"

# Rebuild the mod's own objects when the version changes (the vendored ImGui and
# MinHook objects do not use it, so they are left alone).
$(MOD_OBJS): VERSION

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -DD2RCC_VERSION='"$(VERSION)"' -c $< -o $@

$(BUILD)/imgui/%.o: $(IMGUI_DIR)/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/minhook/%.o: $(MINHOOK_DIR)/src/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/res/%.o: res/%.rc | $(BUILD)
	@mkdir -p $(dir $@)
	$(RC) $(RCFLAGS) -i $< -o $@

$(BUILD)/res/default-config.o: res/cabbycodes.toml
$(BUILD)/res/version.o: $(BUILD)/res/version_gen.h

# The version resource reads its numbers from a header the build writes: windres
# mangles quoted -D values, a generated file does not.
$(BUILD)/res/version_gen.h: VERSION | $(BUILD)
	@mkdir -p $(dir $@)
	@printf '#define RC_VER_MAJOR %s\n#define RC_VER_MINOR %s\n#define RC_VER_PATCH %s\n#define RC_VER_STR "%s"\n' \
	  '$(VER_MAJOR)' '$(VER_MINOR)' '$(VER_PATCH)' '$(VERSION)' > $@

-include $(MOD_OBJS:.o=.d) $(IMGUI_OBJS:.o=.d) $(MINHOOK_OBJS:.o=.d)

$(BUILD):
	mkdir -p $(BUILD)

# `make version` prints the version; `make version X.Y.Z` sets it (`rev` is an
# older spelling of the same thing). Make has no argument syntax, so the new
# version arrives as a second goal; the block near the top defines a do-nothing
# target for it so make does not fail trying to build "1.2.3".
version rev:
	@v='$(REV)'; \
	if [ -z "$$v" ] && [ "$@" = version ]; then echo "$(VERSION)"; exit 0; fi; \
	if ! echo "$$v" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$$'; then \
	  echo "usage: make $@ X.Y.Z        (for example: make $@ 1.2.0)"; \
	  echo "current version: $(VERSION)"; \
	  exit 1; \
	fi; \
	printf '%s\n' "$$v" > VERSION; \
	echo ">> version $(VERSION) -> $$v"; \
	echo ">> run 'make package' to build $(PROJECT)_v$$v.zip"

clean:
	rm -rf build tests/build $(DIST)

# --- Deploy -----------------------------------------------------------------
# D2RLoader loads a plugin from one of two places, and the same plugin must not
# be in both:
#   global:  <game>/d2rloader/plugins/            (every mod, and no mod)
#   mod:     <game>/mods/<MOD_NAME>/d2rloader/plugins/   (that mod only - what
#            D2RMM for D2RLoader installs into)
# `install` uses the global one (handy while developing, D2RMM cannot wipe it);
# `install-mod` the mod one. Each removes the other's copy first. The default
# config is copied only when the user has none yet - D2RLoader itself would
# write the embedded copy, and never overwrites an existing file either.
#
# Atomic copy: write to a temp name in the same directory then rename(2) over
# the target, so a running game keeps its old intact mapping.
define deploy
	set -e; \
	if [ -z "$(GAME_DIR)" ]; then \
	  echo "ERROR: GAME_DIR is not set."; \
	  echo "  Copy config.mk.example to config.mk and set GAME_DIR,"; \
	  echo "  or run: make $(1) GAME_DIR='/path/to/Diablo II Resurrected'"; \
	  exit 1; \
	fi; \
	if [ ! -f "$(GAME_DIR)/D2R.exe" ]; then \
	  echo "ERROR: no 'D2R.exe' in '$(GAME_DIR)' - is that really the game folder?"; exit 1; \
	fi; \
	if [ ! -f "$(GAME_DIR)/D2RLoader.exe" ]; then \
	  echo ">> WARNING: no D2RLoader.exe in '$(GAME_DIR)' - the plugin needs D2RLoader (d2rloader.net)"; \
	fi; \
	dst="$(2)"; other="$(3)"; \
	mkdir -p "$$dst/plugins" "$$dst/config"; \
	if [ -f "$$other/plugins/$(NAME).dll" ]; then \
	  rm -f "$$other/plugins/$(NAME).dll"; echo ">> removed the other copy $$other/plugins/$(NAME).dll"; \
	fi; \
	t="$$dst/plugins/$(NAME).dll.new.$$$$"; cp -f $(TARGET) "$$t"; mv -f "$$t" "$$dst/plugins/$(NAME).dll"; \
	echo ">> installed (atomic) $$dst/plugins/$(NAME).dll"; \
	if [ ! -f "$$dst/config/$(PLUGIN_ID).toml" ]; then \
	  cp res/cabbycodes.toml "$$dst/config/$(PLUGIN_ID).toml"; \
	  echo ">> wrote default settings $$dst/config/$(PLUGIN_ID).toml"; \
	fi
endef

install: $(TARGET)
	@$(call deploy,install,$(GAME_DIR)/d2rloader,$(GAME_DIR)/mods/$(MOD_NAME)/d2rloader)

install-mod: $(TARGET)
	@$(call deploy,install-mod,$(GAME_DIR)/mods/$(MOD_NAME)/d2rloader,$(GAME_DIR)/d2rloader)

# Remove the plugin from both scopes. The settings files are left alone.
uninstall:
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then echo "ERROR: GAME_DIR is not set."; exit 1; fi; \
	for f in "$(GAME_DIR)/d2rloader/plugins/$(NAME).dll" "$(GAME_DIR)/mods/$(MOD_NAME)/d2rloader/plugins/$(NAME).dll"; do \
	  if [ -f "$$f" ]; then rm -f "$$f"; echo ">> removed $$f"; fi; \
	done; \
	echo ">> uninstalled (settings kept in d2rloader/config/$(PLUGIN_ID).toml)"

# --- Distributable ----------------------------------------------------------
# `make dist` assembles dist/D2RCabbyCodes/: a D2RMM mod folder (mod.json,
# mod.js) that carries the D2RLoader plugin in its d2rloader/ subfolder, the way
# D2RMM for D2RLoader expects. It is always the release build (DEV=0).
# `make package` zips it (the D2RMM mod) and, for people who install plugins by
# hand, the d2rloader/ folder alone.
DIST      ?= dist
PAYLOAD    = $(DIST)/$(PROJECT)
PKG        ?= $(DIST)/$(PROJECT)_v$(VERSION).zip
PLUGIN_PKG ?= $(DIST)/$(NAME)_v$(VERSION).zip
RELEASE_DLL := build/release/$(NAME).dll

dist:
	@$(MAKE) --no-print-directory DEV=0 $(RELEASE_DLL)
	@rm -rf "$(PAYLOAD)" && mkdir -p "$(PAYLOAD)/d2rloader/plugins" "$(PAYLOAD)/d2rloader/config"
	@sed 's/@VERSION@/$(VERSION)/' mod/mod.json > "$(PAYLOAD)/mod.json"
	@cp mod/mod.js README.md LICENSE "$(PAYLOAD)/"
	@cp $(RELEASE_DLL) "$(PAYLOAD)/d2rloader/plugins/$(NAME).dll"
	@cp res/cabbycodes.toml "$(PAYLOAD)/d2rloader/config/$(PLUGIN_ID).toml"
	@echo ">> assembled $(PAYLOAD) (release build)"

# The mod folder D2RMM lists: mod.json, mod.js and the d2rloader/ payload.
# Any D2RMM runs mod.js, which changes no game data (the mod has no D2RMM
# settings); D2RMM for D2RLoader also installs the plugin from it.
# D2RMM_MODS_DIR is D2RMM's own mods folder.
install-d2rmm: dist
	@set -e; \
	if [ -z "$(D2RMM_MODS_DIR)" ]; then echo "ERROR: D2RMM_MODS_DIR is not set (see config.mk.example)."; exit 1; fi; \
	if [ ! -d "$(D2RMM_MODS_DIR)" ]; then echo "ERROR: D2RMM_MODS_DIR '$(D2RMM_MODS_DIR)' does not exist."; exit 1; fi; \
	dst="$(D2RMM_MODS_DIR)/$(PROJECT)"; \
	if [ -d "$$dst" ]; then find "$$dst" -mindepth 1 -maxdepth 1 -exec rm -rf {} +; fi; \
	mkdir -p "$$dst"; \
	cp -r "$(PAYLOAD)/." "$$dst/"; \
	echo ">> installed the mod folder $$dst"

package: dist
	@command -v zip >/dev/null || { echo "ERROR: zip required"; exit 1; }
	@rm -f "$(PKG)" "$(PLUGIN_PKG)"
	@cd "$(DIST)" && zip -r -q "$(abspath $(PKG))" "$(PROJECT)"
	@cd "$(PAYLOAD)" && zip -r -q "$(abspath $(PLUGIN_PKG))" d2rloader
	@echo ">> packaged -> $(PKG) ($$(du -h "$(PKG)" | cut -f1)) and $(PLUGIN_PKG)"

# --- The item seed model (research) -------------------------------------------
# tools/seedmodel/ follows the game's item generator roll by roll: what an item's
# own seed makes. The plugin does not use it (no save keeps that seed; the seed a
# save does keep is src/itemseed.*), but tests/test_itemseed.cpp checks the
# plugin's unit seed arithmetic against it. Host builds: `make seedfinder` (the
# search over all seeds; see its head for the options) and `make seedmodel-test`
# (the model's own checks, over the loader's compiled tables when GAME_DIR has
# them).
HOST_CXX   ?= g++
SEEDMODEL  := tools/seedmodel
SEEDFINDER := build/host/seedfinder
SEEDMODEL_TEST := build/host/test_itemgen
$(SEEDFINDER): $(SEEDMODEL)/seedfinder.cpp $(SEEDMODEL)/itemgen.cpp $(SEEDMODEL)/itemgen.h $(SEEDMODEL)/tables.h
	@mkdir -p build/host
	$(HOST_CXX) -std=c++20 -O2 -pthread -Wall -Wextra -I$(SEEDMODEL) -o $@ $(SEEDMODEL)/seedfinder.cpp $(SEEDMODEL)/itemgen.cpp
$(SEEDMODEL_TEST): $(SEEDMODEL)/test_itemgen.cpp $(SEEDMODEL)/itemgen.cpp $(SEEDMODEL)/itemgen.h $(SEEDMODEL)/tables.h
	@mkdir -p build/host
	$(HOST_CXX) -std=c++20 -O2 -Wall -Wextra -I$(SEEDMODEL) -o $@ $(SEEDMODEL)/test_itemgen.cpp $(SEEDMODEL)/itemgen.cpp
seedfinder: $(SEEDFINDER)
seedmodel-test: $(SEEDMODEL_TEST)
	@set -e; if [ -z "$$D2RCC_EXCEL" ] && [ -n "$(GAME_DIR)" ]; then \
	  e="$$(ls -d "$(GAME_DIR)"/d2rloader/data/compiler/*/data/global/excel 2>/dev/null | tail -1)"; \
	  if [ -n "$$e" ]; then export D2RCC_EXCEL="$$e"; fi; fi; \
	$(SEEDMODEL_TEST)

# --- Tests ------------------------------------------------------------------
# Host-side checks of the pure logic (config parsing, signatures, policies),
# built as static Windows exes with the same compiler and run under wine.
TEST_SRCS := $(wildcard tests/test_*.cpp)
TEST_EXES := $(patsubst tests/%.cpp,tests/build/%.exe,$(TEST_SRCS))
# test_itemseed runs over the game's armor rows when it finds them: D2RCC_EXCEL, or the loader's compiled excel
# folder under GAME_DIR.
test: $(TEST_EXES)
	@set -e; if [ -z "$$D2RCC_EXCEL" ] && [ -n "$(GAME_DIR)" ]; then \
	  e="$$(ls -d "$(GAME_DIR)"/d2rloader/data/compiler/*/data/global/excel 2>/dev/null | tail -1)"; \
	  if [ -n "$$e" ]; then export D2RCC_EXCEL="$$e"; fi; fi; \
	for t in $(TEST_EXES); do echo ">> $$t"; WINEDEBUG=-all wine "$$t"; done; echo ">> all tests passed"
tests/build/%.exe: tests/%.cpp $(wildcard src/*.h) $(wildcard tests/*.h) $(wildcard $(SEEDMODEL)/*.h)
	@mkdir -p tests/build
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
	  -DD2RCC_VERSION='"$(VERSION)"' \
	  -I$(SDK_INC) -Isrc -I$(SEEDMODEL) -static -o $@ $< $(TEST_EXTRA_$(notdir $(basename $<)))
TEST_EXTRA_test_config := src/config.cpp
TEST_EXTRA_test_presets := src/presets.cpp
TEST_EXTRA_test_itemseed := $(SEEDMODEL)/itemgen.cpp
TEST_EXTRA_test_dropodds := src/dropodds.cpp src/mem.cpp
TEST_EXTRA_test_perf := src/perf.cpp
TEST_EXTRA_test_log := src/log.cpp src/context.cpp
$(foreach t,$(TEST_SRCS),$(eval tests/build/$(notdir $(basename $(t))).exe: $(TEST_EXTRA_$(notdir $(basename $(t))))))
