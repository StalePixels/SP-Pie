#
# SP-Pie video service for the Raspberry Pi Zero 2 W (Circle, AArch64).
#
#   gmake               build kernel8.img
#   gmake OVERLAY=0     build without the on-screen instrumentation lines
#   gmake test          build and run the host tests (no Circle world needed)
#   gmake clean
#
# Needs GNU make 4 or later, the aarch64-none-elf toolchain on PATH, and the
# rpi3 Circle world built inside ../circle-libsdl2 (see README.md).
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

OBJS	= src/main.o src/kernel.o src/sppieapp.o src/smieventreader.o \
	  src/intake.o src/spectrumstate.o src/renderer.o src/overlay.o

DEFINE	+= -DSPPIE_OVERLAY=$(OVERLAY)

EXTRACLEAN = src/*.o src/*.d

include $(CIRCLEHOME)/Rules.mk

INCLUDE := $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

LIBS := $(CIRCLE_STDLIB_LIBS)

# Objects depend on the overlay choice, which is not visible in timestamps.
$(OBJS): .overlay-$(OVERLAY)
.overlay-$(OVERLAY):
	@rm -f .overlay-*
	@touch $@

-include $(DEPS)

endif

.PHONY: test test-clean
test:
	$(MAKE) -C test

test-clean:
	$(MAKE) -C test clean
