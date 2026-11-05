/*
 * config.h
 *
 * The build configuration that Fuse and libspectrum read from "config.h".
 * Their own build makes it with autoconf. Here only the Fuse display, the
 * memory and paging code, the machine definitions and the libspectrum
 * timing tables are built, for the Pi (bare metal, newlib) and for the host
 * tests, so the file is written by hand.
 *
 * It also serves libspectrum's make-perl.c, which writes the type
 * definitions into the generated libspectrum.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SPPIE_FUSEULA_CONFIG_H
#define SPPIE_FUSEULA_CONFIG_H

/* libspectrum.h uses <stdint.h> types and its own GLib replacement. */
#define HAVE_STDINT_H 1

/* compat.h includes Fuse's own getopt header unless this is set. Nothing in
   the files built here parses options. */
#define HAVE_GETOPT_LONG 1

/* libspectrum.c reports this as its version. It is the release that
   the libspectrum submodule is pinned to. */
#define VERSION "1.8.0"

#endif
