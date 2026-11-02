/*
 * fusestubs.h
 *
 * The parts of Fuse that the ULA model and the sound generator call but
 * that SP-Pie does not build: the Z80, tape, disk, the debugger, the user
 * interface and the other peripherals, and the sound entry points of the
 * display side of Fuse. fusestubs.c gives each one a definition that does
 * nothing, or the value Fuse has with the peripheral absent.
 *
 * Fuse's start-up manager is replaced too: it records each module's init
 * function, and fusestubs_run_startup () calls them in the order they were
 * registered.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SPPIE_FUSESTUBS_H
#define SPPIE_FUSESTUBS_H

/* Calls the recorded init functions. Returns 0 when all succeed. */
int fusestubs_run_startup( void );

#endif
