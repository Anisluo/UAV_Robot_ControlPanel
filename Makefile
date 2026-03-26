CXX ?= g++
MOC ?= $(shell pkg-config --variable=host_bins Qt5Core)/moc

QT_CFLAGS := $(shell pkg-config --cflags Qt5Widgets Qt5Network)
QT_LIBS   := $(shell pkg-config --libs   Qt5Widgets Qt5Network)

CXXFLAGS := -std=c++17 -Wall -Wextra -g -O2 -fPIC \
            -I src -I src/core -I src/gui \
            $(QT_CFLAGS) \
            -MMD -MP

BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
MOC_DIR   := $(BUILD_DIR)/moc
BIN_DIR   := $(BUILD_DIR)/bin
TARGET    := $(BIN_DIR)/HostGUI

# ── Sources ────────────────────────────────────────────────────────────────
SRCS := $(shell find src -name '*.cpp')
OBJS := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# ── MOC helpers ────────────────────────────────────────────────────────────
MOC_HDRS := $(shell grep -rl 'Q_OBJECT' src --include='*.h')

moc_src = $(MOC_DIR)/moc_$(notdir $(basename $(1))).cpp
moc_obj = $(OBJ_DIR)/moc/moc_$(notdir $(basename $(1))).o

# Lazily-evaluated so it's always consistent with MOC_HDRS
MOC_OBJS = $(foreach h,$(MOC_HDRS),$(call moc_obj,$(h)))

# ── Default goal must come before foreach/eval to avoid being displaced ────
.PHONY: all clean install-deps
all: $(TARGET)

# ── Per-header moc rules (generation + compilation, fully explicit) ────────
define MOC_RULES
$(call moc_src,$(1)): $(1)
	@mkdir -p $(MOC_DIR)
	$(MOC) $(QT_CFLAGS) $$< -o $$@

$(call moc_obj,$(1)): $(call moc_src,$(1))
	@mkdir -p $(OBJ_DIR)/moc
	$(CXX) $(CXXFLAGS) -c $$< -o $$@
endef
$(foreach h,$(MOC_HDRS),$(eval $(call MOC_RULES,$(h))))

# ── Link ───────────────────────────────────────────────────────────────────
$(TARGET): $(OBJS) $(MOC_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $^ -o $@ $(QT_LIBS)

# ── Compile regular sources ────────────────────────────────────────────────
$(OBJ_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ── Install Qt5 dev packages (Ubuntu) ─────────────────────────────────────
install-deps:
	sudo apt-get update && sudo apt-get install -y qtbase5-dev pkg-config

# ── Auto-generated header dependencies ────────────────────────────────────
DEPS := $(OBJS:.o=.d)
-include $(DEPS)

# ── Clean ──────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
