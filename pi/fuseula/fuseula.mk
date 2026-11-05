#
# fuseula.mk
#
# The Fuse sources the Pi kernel and the host tests build, unmodified, and
# the generated headers they need. Included by ../Makefile and
# ../test/Makefile, which set FUSEULA_ROOT to the path of the repository root from their
# own directory.
#
# Fuse and libspectrum are pinned submodules (fuse/ and
# libspectrum/). Their own builds use autoconf; here the headers that
# autoconf and their Perl scripts would make are generated into FUSEULA_GEN
# with the same scripts:
#
#   libspectrum.h  libspectrum's make-perl.c (host C compiler) and generate.pl
#   settings.h     Fuse's settings-header.pl
#   options.h      Fuse's cpp-perl.pl and ui/widget/options-header.pl
#   snap_accessors.c  libspectrum's accessor.pl
#
# Needs perl and a host C compiler (HOSTCC).
#

# The rules below must not become the including Makefile's default goal.
FUSEULA_SAVED_GOAL := $(.DEFAULT_GOAL)

FUSE_DIR        = $(FUSEULA_ROOT)/fuse
LIBSPECTRUM_DIR = $(FUSEULA_ROOT)/libspectrum
FUSEULA_DIR     = $(FUSEULA_ROOT)/pi/fuseula
FUSEULA_GEN    ?= $(FUSEULA_DIR)/gen

HOSTCC ?= cc
PERL   ?= perl

# Fuse: the ULA display model, memory and paging, the machine definitions of
# the 48K, 128K, +2, +2A and +3, the peripheral port decode, the ULA port,
# and the sound generator with its Blip_Buffer and filters.
FUSEULA_FUSE_SOURCES = \
	$(FUSE_DIR)/display.c \
	$(FUSE_DIR)/rectangle.c \
	$(FUSE_DIR)/memory_pages.c \
	$(FUSE_DIR)/machine.c \
	$(FUSE_DIR)/spectrum.c \
	$(FUSE_DIR)/periph.c \
	$(FUSE_DIR)/peripherals/ula.c \
	$(FUSE_DIR)/machines/machines_periph.c \
	$(FUSE_DIR)/machines/spec48.c \
	$(FUSE_DIR)/machines/spec128.c \
	$(FUSE_DIR)/machines/specplus2.c \
	$(FUSE_DIR)/machines/specplus2a.c \
	$(FUSE_DIR)/machines/specplus3.c \
	$(FUSE_DIR)/sound/blipbuffer.c \
	$(FUSE_DIR)/sound/speaker_filter.c \
	$(FUSE_DIR)/sound/ula_filter.c \
	$(FUSEULA_SOUND_GENERATORS)

# Fuse's sound generator, sound.c, compiled once for each generator with
# its names prefixed (fusesound_rename.h). The wrappers hold Fuse's code
# only, so they build with Fuse's flags.
FUSEULA_SOUND_GENERATORS = \
	$(FUSEULA_DIR)/fusesound_ay0.c \
	$(FUSEULA_DIR)/fusesound_ay1.c \
	$(FUSEULA_DIR)/fusesound_ula48.c \
	$(FUSEULA_DIR)/fusesound_ula128.c

# libspectrum: the machine timing tables, machine capabilities, memory
# allocation, the snapshot accessors (generated) that Fuse's snapshot hooks
# refer to, and the GLib replacement Fuse's lists use.
FUSEULA_LIBSPECTRUM_SOURCES = \
	$(LIBSPECTRUM_DIR)/timings.c \
	$(LIBSPECTRUM_DIR)/libspectrum.c \
	$(LIBSPECTRUM_DIR)/memory.c \
	$(LIBSPECTRUM_DIR)/myglib/garray.c \
	$(LIBSPECTRUM_DIR)/myglib/ghash.c \
	$(LIBSPECTRUM_DIR)/myglib/glock.c \
	$(LIBSPECTRUM_DIR)/myglib/gslist.c \
	$(FUSEULA_GEN)/snap_accessors.c

# Our layer: the C interface to the C++ code, the display output, ULAplus,
# the border outside Fuse's display area, the sound port decode and mix,
# and stand-ins for the parts of Fuse that are not built.
FUSEULA_OWN_SOURCES = \
	$(FUSEULA_DIR)/fuseula.c \
	$(FUSEULA_DIR)/fusesound.c \
	$(FUSEULA_DIR)/fusestubs.c

FUSEULA_SOURCES = $(FUSEULA_FUSE_SOURCES) $(FUSEULA_LIBSPECTRUM_SOURCES) $(FUSEULA_OWN_SOURCES)

FUSEULA_HEADERS = $(FUSEULA_GEN)/libspectrum.h $(FUSEULA_GEN)/settings.h $(FUSEULA_GEN)/options.h

# Our config.h and the generated headers come before the Fuse tree, so that
# <config.h> and "libspectrum.h" resolve to them.
FUSEULA_INCLUDES = -I$(FUSEULA_DIR) -I$(FUSEULA_GEN) -I$(FUSE_DIR) -I$(LIBSPECTRUM_DIR)

# Warnings in the vendored code are Fuse's; they are not changed here.
FUSEULA_VENDOR_CFLAGS = -w

$(FUSEULA_GEN)/make-perl: $(LIBSPECTRUM_DIR)/make-perl.c $(FUSEULA_DIR)/config.h
	@mkdir -p $(FUSEULA_GEN)
	$(HOSTCC) -I$(FUSEULA_DIR) -o $@ $<

$(FUSEULA_GEN)/generate.pl: $(FUSEULA_GEN)/make-perl $(LIBSPECTRUM_DIR)/generate.pl.in
	$(FUSEULA_GEN)/make-perl > $@.tmp
	cat $(LIBSPECTRUM_DIR)/generate.pl.in >> $@.tmp
	mv $@.tmp $@

$(FUSEULA_GEN)/libspectrum.h: $(FUSEULA_GEN)/generate.pl $(LIBSPECTRUM_DIR)/libspectrum.h.in \
			      $(LIBSPECTRUM_DIR)/snap_accessors.txt $(LIBSPECTRUM_DIR)/tape_accessors.txt
	$(PERL) -p $(FUSEULA_GEN)/generate.pl $(LIBSPECTRUM_DIR) $(LIBSPECTRUM_DIR)/libspectrum.h.in > $@.tmp
	mv $@.tmp $@

$(FUSEULA_GEN)/settings.h: $(FUSE_DIR)/settings-header.pl $(FUSE_DIR)/settings.dat
	@mkdir -p $(FUSEULA_GEN)
	$(PERL) -I$(FUSE_DIR)/perl $(FUSE_DIR)/settings-header.pl $(FUSE_DIR)/settings.dat > $@.tmp
	mv $@.tmp $@

$(FUSEULA_GEN)/options.h: $(FUSE_DIR)/perl/cpp-perl.pl $(FUSE_DIR)/ui/options.dat \
			  $(FUSE_DIR)/ui/widget/options-header.pl $(FUSEULA_DIR)/config.h
	@mkdir -p $(FUSEULA_GEN)
	$(PERL) $(FUSE_DIR)/perl/cpp-perl.pl $(FUSEULA_DIR)/config.h $(FUSE_DIR)/ui/options.dat \
		| $(PERL) -I$(FUSE_DIR)/perl $(FUSE_DIR)/ui/widget/options-header.pl - public > $@.tmp
	mv $@.tmp $@

$(FUSEULA_GEN)/snap_accessors.c: $(LIBSPECTRUM_DIR)/accessor.pl $(LIBSPECTRUM_DIR)/snap_accessors.txt
	@mkdir -p $(FUSEULA_GEN)
	$(PERL) $(LIBSPECTRUM_DIR)/accessor.pl $(LIBSPECTRUM_DIR)/snap_accessors.txt > $@.tmp
	mv $@.tmp $@

.DEFAULT_GOAL := $(FUSEULA_SAVED_GOAL)
