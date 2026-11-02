#
# SP-Pie video service for the Raspberry Pi Zero 2 W (Circle, AArch64).
#
#   gmake               build kernel8.img
#   gmake OVERLAY=0     build without the on-screen instrumentation lines
#   gmake test          build and run the host tests (no Circle world needed)
#   gmake clean
#
# Needs GNU make 4 or later, the aarch64-none-elf toolchain on PATH, the
# rpi3 Circle world built inside ../circle-libsdl2, perl and a host C
# compiler (for the Fuse headers, see fuseula/fuseula.mk) (see README.md).
#

CIRCLE_WORLD ?= ../circle-libsdl2/circle-stdlib-rpi3
OVERLAY      ?= 1

ifneq ($(filter-out test test-clean,$(MAKECMDGOALS)),)
NEED_WORLD = 1
endif
ifeq ($(MAKECMDGOALS),)
NEED_WORLD = 1
endif

ifeq ($(NEED_WORLD),1)

ifeq ($(wildcard $(CIRCLE_WORLD)/Config.mk),)
$(error $(CIRCLE_WORLD)/Config.mk is missing: build the rpi3 world first (see README.md))
endif

include $(CIRCLE_WORLD)/Config.mk

FUSEULA_ROOT = ..
include fuseula/fuseula.mk

# The Fuse sources and our C layer, compiled into fuseula/obj so that the
# submodules stay clean.
FUSEULA_OBJDIR = fuseula/obj
FUSEULA_OBJS = $(addprefix $(FUSEULA_OBJDIR)/,$(notdir $(FUSEULA_SOURCES:.c=.o)))

OBJS	= src/main.o src/kernel.o src/sppieapp.o src/smieventreader.o \
	  src/intake.o src/tstateclock.o src/ulaclock.o src/frameclock.o src/presenter.o \
	  src/audiostream.o src/overlay.o $(FUSEULA_OBJS)

DEFINE	+= -DSPPIE_OVERLAY=$(OVERLAY)

EXTRACLEAN = src/*.o src/*.d $(FUSEULA_OBJDIR)/*.o $(FUSEULA_OBJDIR)/*.d $(FUSEULA_GEN)/*

include $(CIRCLEHOME)/Rules.mk

INCLUDE := $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# Circle's sound library, for the HDMI sound device.
LIBS := $(CIRCLEHOME)/lib/sound/libsound.a $(CIRCLE_STDLIB_LIBS)

# Objects depend on the overlay choice, which is not visible in timestamps.
$(OBJS): .overlay-$(OVERLAY)
.overlay-$(OVERLAY):
	@rm -f .overlay-*
	@touch $@

# The C++ code includes fuseula.h only; the generated headers come first
# for the C files.
$(filter src/%.o,$(OBJS)): $(FUSEULA_HEADERS)

# One rule per C file: $(1) the source, $(2) extra flags. Our include paths
# come before Circle's, so Fuse's own headers win. circle-stdlib puts the
# libc++ headers on the C include path too; its <stdatomic.h> is for C++
# only, so the compiler's own <stdatomic.h> goes first for these C files.
FUSEULA_C_SYSTEM = -isystem "$(shell $(CC) -print-file-name=include)"
define FUSEULA_RULE
$(FUSEULA_OBJDIR)/$(notdir $(1:.c=.o)): $(1) $(FUSEULA_HEADERS) $(FUSEULA_DIR)/config.h
	@mkdir -p $(FUSEULA_OBJDIR)
	@echo "  CC    $$@"
	@$$(CC) $(FUSEULA_INCLUDES) $(FUSEULA_C_SYSTEM) $$(CFLAGS) $$(C_STANDARD) $(2) -MMD -MP -c -o $$@ $$<
endef
$(foreach f,$(FUSEULA_FUSE_SOURCES) $(FUSEULA_LIBSPECTRUM_SOURCES),$(eval $(call FUSEULA_RULE,$(f),$(FUSEULA_VENDOR_CFLAGS))))
$(foreach f,$(FUSEULA_OWN_SOURCES),$(eval $(call FUSEULA_RULE,$(f),)))

-include $(DEPS)
-include $(wildcard $(FUSEULA_OBJDIR)/*.d)

endif

.PHONY: test test-clean
test:
	$(MAKE) -C test

test-clean:
	$(MAKE) -C test clean
