//
// event.h
//
// Event words from the CPLD, as defined in docs/protocol.md.
//
// The CPLD sends every Z80 memory write and every I/O write as one event,
// with the full address and the data byte. It does no decode; the Pi decides
// what each write means (see spectrumstate.h).
//
// One event is two 16-bit SMI reads. With 16-bit SMI and DMA packing, the
// pair arrives as one 32-bit word: half-word 0 in bits 15..0, half-word 1 in
// bits 31..16.
//
// Half-word 0:  bit 15 H = 0, bit 14 V, bit 13 OVF, bit 12 F,
//               bits 11..10 KIND, bit 9 zero, bit 8 A15, bits 7..0 DATA
// Half-word 1:  bit 15 H = 1, bits 14..0 A14..A0
//
// This file has no platform dependencies so that it builds on the host.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_event_h
#define _sppie_event_h

#include <stdint.h>

namespace sppie {

enum EventKind : uint8_t
{
	KindMemory	= 0,	// memory write: address, data
	KindIo		= 1,	// I/O write: 16-bit port address, data
	KindReset	= 2,	// /RESET went high; address and data are 0
	KindReserved	= 3	// never sent
};

struct TEvent
{
	uint8_t		Kind;
	uint8_t		Data;
	uint16_t	Address;
	bool		Overflow;	// OVF: first event stored after the CPLD dropped events
	bool		Frame;		// F: first event stored after an interrupt acknowledge
};

enum TWordKind
{
	WordEmpty,		// the empty pair: half-word 0 = 0x0000, half-word 1 = 0x8000
	WordEvent,		// V = 1, H bits correct, fields valid
	WordMismatch,		// an H bit is wrong: Pi and CPLD disagree on the half
	WordMalformed		// H bits correct, but the word is neither the exact
				// empty pair nor a valid event
};

// Bit positions in the packed 32-bit word.
static const uint32_t HalfH		= 1u << 15;
static const uint32_t Hw0V		= 1u << 14;
static const uint32_t Hw0Ovf		= 1u << 13;
static const uint32_t Hw0Frame		= 1u << 12;
static const unsigned Hw0KindShift	= 10;
static const uint32_t Hw0KindMask	= 3u << Hw0KindShift;
static const uint32_t Hw0Reserved	= 1u << 9;
static const uint32_t Hw0A15		= 1u << 8;
static const uint32_t Hw0DataMask	= 0xFFu;
static const uint32_t Hw1AddressMask	= 0x7FFFu;

// The empty pair, sent when the queue is empty: 0x0000 then 0x8000.
static const uint32_t EmptyPairWord	= HalfH << 16;

// Classifies one packed word. Fills rEvent only for WordEvent.
//
// Checks, in order:
//  - half-word 0 must have H = 0 and half-word 1 must have H = 1 (any pair,
//    empty or not), otherwise WordMismatch;
//  - V = 0 gives WordEmpty only for the exact empty pair (0x0000, 0x8000);
//    any other V = 0 word is WordMalformed;
//  - for V = 1: KIND 11, a set bit 9, or a reset event with a non-zero
//    address or DATA give WordMalformed. OVF and F may be set on any event.
static inline TWordKind DecodeWord (uint32_t nWord, TEvent &rEvent)
{
	uint32_t nHw0 = nWord & 0xFFFFu;
	uint32_t nHw1 = nWord >> 16;

	if ((nHw0 & HalfH) != 0 || (nHw1 & HalfH) == 0)
	{
		return WordMismatch;
	}

	if ((nHw0 & Hw0V) == 0)
	{
		return nWord == EmptyPairWord ? WordEmpty : WordMalformed;
	}

	uint8_t nKind = (uint8_t) ((nHw0 & Hw0KindMask) >> Hw0KindShift);
	uint16_t nAddress = (uint16_t) (((nHw0 & Hw0A15) << 7) | (nHw1 & Hw1AddressMask));
	uint8_t nData = (uint8_t) (nHw0 & Hw0DataMask);

	if (   nKind == KindReserved
	    || (nHw0 & Hw0Reserved) != 0
	    || (nKind == KindReset && (nAddress != 0 || nData != 0)))
	{
		return WordMalformed;
	}

	rEvent.Kind = nKind;
	rEvent.Data = nData;
	rEvent.Address = nAddress;
	rEvent.Overflow = (nHw0 & Hw0Ovf) != 0;
	rEvent.Frame = (nHw0 & Hw0Frame) != 0;

	return WordEvent;
}

// Builds a packed word. Used by the host tests to make synthetic streams.
static inline uint32_t EncodeEvent (uint8_t nKind, uint16_t nAddress, uint8_t nData,
				    bool bOverflow = false, bool bFrame = false)
{
	uint32_t nHw0 =   Hw0V | ((uint32_t) (nKind & 3) << Hw0KindShift)
			| ((uint32_t) (nAddress >> 15) << 8) | nData;
	if (bOverflow)
	{
		nHw0 |= Hw0Ovf;
	}
	if (bFrame)
	{
		nHw0 |= Hw0Frame;
	}
	uint32_t nHw1 = HalfH | (nAddress & Hw1AddressMask);

	return nHw0 | (nHw1 << 16);
}

}

#endif
