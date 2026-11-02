//
// event.h
//
// Event words from the CPLD, as defined in docs/protocol.md.
//
// One event is two 16-bit SMI reads. With 16-bit SMI and DMA packing, the
// pair arrives as one 32-bit word: half-word 0 in bits 15..0, half-word 1 in
// bits 31..16.
//
// Half-word 0:  bit 15 H = 0, bit 14 V, bit 13 OVF, bits 12..10 TYPE,
//               bits 9..8 zero, bits 7..0 DATA
// Half-word 1:  bit 15 H = 1, bits 14..13 zero, bits 12..0 OFFSET
//
// This file has no platform dependencies so that it builds on the host.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_event_h
#define _sppie_event_h

#include <stdint.h>

namespace sppie {

enum EventType : uint8_t
{
	EventUnused	= 0,
	EventBank5	= 1,	// write to RAM bank 5, OFFSET < 0x1B00
	EventBank7	= 2,	// write to RAM bank 7, OFFSET < 0x1B00
	EventPortFE	= 3,	// write to port 0xFE
	EventUlaReg	= 4,	// write to ULAplus register port 0xBF3B
	EventUlaData	= 5,	// write to ULAplus data port 0xFF3B
	EventPort7FFD	= 6,	// accepted write to port 0x7FFD
	EventPort1FFD	= 7	// accepted write to port 0x1FFD
};

static const uint16_t ScreenBankBytes = 0x1B00;	// 6912: pixels and attributes

struct TEvent
{
	uint8_t		Type;
	uint8_t		Data;
	uint16_t	Offset;
	bool		Overflow;	// OVF: first event queued after the CPLD dropped events
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
static const unsigned Hw0TypeShift	= 10;
static const uint32_t Hw0TypeMask	= 7u << Hw0TypeShift;
static const uint32_t Hw0Reserved	= 3u << 8;
static const uint32_t Hw0DataMask	= 0xFFu;
static const uint32_t Hw1Reserved	= 3u << 13;
static const uint32_t Hw1OffsetMask	= 0x1FFFu;

// The empty pair, sent when the queue is empty: 0x0000 then 0x8000.
static const uint32_t EmptyPairWord	= HalfH << 16;

// Classifies one packed word. Fills rEvent only for WordEvent.
//
// Checks, in order:
//  - half-word 0 must have H = 0 and half-word 1 must have H = 1 (any pair,
//    empty or not), otherwise WordMismatch;
//  - V = 0 gives WordEmpty only for the exact empty pair (0x0000, 0x8000);
//    any other V = 0 word is WordMalformed;
//  - for V = 1: TYPE 0, a set reserved bit, an OFFSET at or above 0x1B00 for
//    TYPE 1 and 2, or a non-zero OFFSET for other types give WordMalformed.
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

	uint8_t nType = (uint8_t) ((nHw0 & Hw0TypeMask) >> Hw0TypeShift);
	uint16_t nOffset = (uint16_t) (nHw1 & Hw1OffsetMask);

	if (   nType == EventUnused
	    || (nHw0 & Hw0Reserved) != 0
	    || (nHw1 & Hw1Reserved) != 0)
	{
		return WordMalformed;
	}

	if (nType == EventBank5 || nType == EventBank7)
	{
		if (nOffset >= ScreenBankBytes)
		{
			return WordMalformed;
		}
	}
	else if (nOffset != 0)
	{
		return WordMalformed;
	}

	rEvent.Type = nType;
	rEvent.Data = (uint8_t) (nHw0 & Hw0DataMask);
	rEvent.Offset = nOffset;
	rEvent.Overflow = (nHw0 & Hw0Ovf) != 0;

	return WordEvent;
}

// Builds a packed word. Used by the host tests to make synthetic streams.
static inline uint32_t EncodeEvent (uint8_t nType, uint8_t nData, uint16_t nOffset,
				    bool bOverflow = false)
{
	uint32_t nHw0 = Hw0V | ((uint32_t) (nType & 7) << Hw0TypeShift) | nData;
	if (bOverflow)
	{
		nHw0 |= Hw0Ovf;
	}
	uint32_t nHw1 = HalfH | (nOffset & Hw1OffsetMask);

	return nHw0 | (nHw1 << 16);
}

}

#endif
