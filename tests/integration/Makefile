# ============================================================================
# tests/integration/Makefile
# Incremental, parallel cross-compile of the plugin to a Windows x64 DLL with
# mingw-w64. This is the build engine; build.sh is a thin wrapper around it.
#
# Why a Makefile (vs. the old flat script): it does the two things Visual Studio
# does that a script doesn't —
#   1. Incremental: a TU is recompiled only when it, or a header it includes,
#      actually changed (via gcc -MMD dependency files). Edit one .cpp and only
#      that .o + the link rebuild.
#   2. Parallel: `make -jN` compiles independent TUs concurrently.
# Plus ccache (if installed) caches objects across even a `make clean`.
#
# Usage:
#   make -j$(nproc)     # build (incremental)
#   make clean          # remove objects + DLL
#   make print-config   # show tools/flags
# ============================================================================

# --- paths -------------------------------------------------------------------
ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC     := $(ROOT)/mxbmrp3
BUILD   := $(CURDIR)/build
OBJDIR  := $(BUILD)/obj
TARGET  := $(BUILD)/mxbmrp3_test.dlo

# --- toolchain (ccache-wrapped when available) -------------------------------
CCACHE  := $(shell command -v ccache 2>/dev/null)
CXX     := $(CCACHE) x86_64-w64-mingw32-g++
CC      := $(CCACHE) x86_64-w64-mingw32-gcc

DEFS    := -DGAME_MXBIKES -DNOMINMAX -DNDEBUG -DMXBMRP3_TEST_BUILD -DMXBMRP3_ALLOW_NO_ANALYTICS
INCS    := -I$(CURDIR)/shim -I$(SRC)
CXXFLAGS := -std=c++17 -m64 -O1 -w $(DEFS) $(INCS)
CFLAGS   := -m64 -O1 -w $(DEFS) $(INCS)
LDFLAGS  := -m64 -shared -static -static-libgcc -static-libstdc++
LIBS     := -lws2_32 -lwinhttp -lbcrypt -ldbghelp -lxinput -lwinmm -lole32 -lshlwapi -luser32 -lgdi32 -ldwmapi

# --- sources (paths relative to $(SRC)) --------------------------------------
# Excluded: discord_manager (GAME_HAS_DISCORD=0 under MXBMRP3_TEST_BUILD, so its calls
# in plugin_manager.cpp compile out and it never links). analytics_manager IS built now:
# GAME_HAS_ANALYTICS is also 0 in the test build, so plugin_manager never auto-inits it
# (no beacon threads at startup) — but the TU is compiled so the analytics-wiring test can
# drive its payload build + sampling gate directly through hooks (it never sends: the
# capture seam suppresses real network sends in a test build). Only the MXB api-export TU
# is built (not gpb/krp).
EXCLUDE  := discord_manager
CPP_SRCS := $(shell cd $(SRC) && find core handlers hud diagnostics -name '*.cpp' | grep -vE '$(EXCLUDE)') vendor/piboso/mxb_api.cpp
C_SRCS   := $(shell cd $(SRC) && find vendor/miniz -name '*.c')

OBJS := $(addprefix $(OBJDIR)/,$(CPP_SRCS:.cpp=.o)) $(addprefix $(OBJDIR)/,$(C_SRCS:.c=.o))
DEPS := $(OBJS:.o=.d)

# --- rules -------------------------------------------------------------------
.PHONY: all clean print-config
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "  LINK  $(notdir $@)  ($(words $(OBJS)) objects)"
	@$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)
	@echo "  DONE  $$(x86_64-w64-mingw32-nm -g --defined-only $@ 2>/dev/null | grep -cE ' T ') exported symbols"

# -MMD -MP: emit a .d file listing every header the TU pulled in, so a header
# edit invalidates exactly the TUs that include it (this is the incremental core).
$(OBJDIR)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $*"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $*"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

print-config:
	@echo "CXX     = $(CXX)"
	@echo "TUs     = $(words $(CPP_SRCS)) C++  +  $(words $(C_SRCS)) C"
	@echo "ccache  = $(if $(CCACHE),$(CCACHE),not installed)"
	@echo "TARGET  = $(TARGET)"

# Pull in per-object header dependencies (silent if absent, e.g. first build).
-include $(DEPS)
