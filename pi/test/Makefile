#
# Host tests: decoder, intake, state model, frame lock, timeline, event log,
# presenter and renderer, built with the host C++ compiler and run on the
# build machine.
#
#   gmake           build and run
#   gmake clean
#

HOSTCXX  ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror

SRCDIR   = ../src
BUILDDIR = build

SOURCES  = test_main.cpp $(SRCDIR)/spectrumstate.cpp $(SRCDIR)/intake.cpp \
	   $(SRCDIR)/renderer.cpp $(SRCDIR)/framelock.cpp $(SRCDIR)/timeline.cpp \
	   $(SRCDIR)/presenter.cpp
HEADERS  = $(wildcard $(SRCDIR)/*.h)

.PHONY: test clean

test: $(BUILDDIR)/sppie_tests
	./$(BUILDDIR)/sppie_tests

$(BUILDDIR)/sppie_tests: $(SOURCES) $(HEADERS)
	@mkdir -p $(BUILDDIR)
	$(HOSTCXX) $(CXXFLAGS) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILDDIR)
