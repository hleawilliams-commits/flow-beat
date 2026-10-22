# SPDX-License-Identifier: GPL-3.0-only
# Exact-firmware passive experiment; build output is never a release by itself.
MOD_ID := encoder-task-observer
SRCS := mod.c observer.cpp abi/submission-entry.S abi/executor-entry.S
AZ_SDK_INCLUDE ?= ../include
OUT ?= out
DEBUG ?= 0
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS := -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden \
          -fno-omit-frame-pointer -fexceptions -fasynchronous-unwind-tables \
          -I$(AZ_SDK_INCLUDE)
CXXFLAGS := $(CFLAGS) -std=c++17 -fvisibility-inlines-hidden
ifeq ($(strip $(DEBUG)),1)
CFLAGS += -g
CXXFLAGS += -g
endif
HEADERS := $(shell find deps -type f -name '*.h') $(AZ_SDK_INCLUDE)/azmod-mod.h
OBJS := $(patsubst %,$(OUT)/obj/%.o,$(SRCS))
.PHONY: all
all: $(OUT)/$(MOD_ID).so
$(OUT)/obj/%.c.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/obj/%.cpp.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/obj/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/exports.map:
	@mkdir -p $(dir $@)
	printf '{ global: az_mod_v1_entry; local: *; };\n' > $@
$(OUT)/$(MOD_ID).so: $(OBJS) $(OUT)/exports.map
	$(CXX) -shared -Wl,--no-undefined -Wl,--hash-style=gnu \
	  -Wl,--version-script=$(OUT)/exports.map -o $@ $(OBJS) -lpthread -lm
