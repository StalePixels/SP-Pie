#
# Host tests: decoder, intake, clock count, frame clock, event log,
# presenter, and the Fuse ULA model with our layer, built with the host
# compilers and run on the build machine.
#
#   gmake           build and run
#   gmake clean
#

HOSTCXX  ?= c++
HOSTCC   ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror
CFLAGS   ?= -std=gnu11 -O2

BUILDDIR = build

FUSEULA_ROOT = ../..
FUSEULA_GEN  = $(BUILDDIR)/gen
include ../fuseula/fuseula.mk

SRCDIR   = ../src
SOURCES  = test_main.cpp $(SRCDIR)/intake.cpp $(SRCDIR)/tstateclock.cpp \
	   $(SRCDIR)/frameclock.cpp $(SRCDIR)/presenter.cpp
HEADERS  = $(wildcard $(SRCDIR)/*.h) $(FUSEULA_DIR)/fuseula.h

# Our own C files build with warnings as errors; Fuse's and libspectrum's
# keep their own warnings, which are not ours to change, so they are off.
OWN_CFLAGS    = $(CFLAGS) -Wall -Wextra -Werror
VENDOR_CFLAGS = $(CFLAGS) $(FUSEULA_VENDOR_CFLAGS)

FUSEULA_OBJDIR = $(BUILDDIR)/fuseula
FUSEULA_OBJS = $(addprefix $(FUSEULA_OBJDIR)/,$(notdir $(FUSEULA_SOURCES:.c=.o)))

.DEFAULT_GOAL := test
.PHONY: test clean

test: $(BUILDDIR)/sppie_tests
	./$(BUILDDIR)/sppie_tests

$(BUILDDIR)/sppie_tests: $(SOURCES) $(HEADERS) $(FUSEULA_OBJS)
	@mkdir -p $(BUILDDIR)
	$(HOSTCXX) $(CXXFLAGS) -o $@ $(SOURCES) $(FUSEULA_OBJS)

define FUSEULA_TEST_RULE
$(FUSEULA_OBJDIR)/$(notdir $(1:.c=.o)): $(1) $(FUSEULA_HEADERS) $(FUSEULA_DIR)/config.h
	@mkdir -p $(FUSEULA_OBJDIR)
	$(HOSTCC) $(FUSEULA_INCLUDES) $(2) -MMD -MP -c -o $$@ $$<
endef
$(foreach f,$(FUSEULA_FUSE_SOURCES) $(FUSEULA_LIBSPECTRUM_SOURCES),$(eval $(call FUSEULA_TEST_RULE,$(f),$(VENDOR_CFLAGS))))
$(foreach f,$(FUSEULA_OWN_SOURCES),$(eval $(call FUSEULA_TEST_RULE,$(f),$(OWN_CFLAGS))))

-include $(wildcard $(FUSEULA_OBJDIR)/*.d)

clean:
	rm -rf $(BUILDDIR)
