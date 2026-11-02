//
// event.h
//
// Read words from the CPLD, as defined in docs/protocol.md.
//
// The CPLD sends every Z80 memory write and every I/O write as one event,
// with the full address, the data byte and its time count. It does no
// decode; Fuse decides what each write means (see presenter.h).
//
// SMI runs in 18-bit mode. Each read is one ring word, with the read word
// in bits 17..0. A pair is two reads, R0 then R1.
//
// Event R0:   bit 17 T4, bit 16 T3, bit 15 H = 0, bit 14 V = 1, bit 13 OVF,
//             bit 12 F, bits 11..10 KIND, bit 9 T2, bit 8 A15, bits 7..0 DATA
// Event R1:   bit 17 T1, bit 16 T0, bit 15 H = 1, bits 14..0 A14..A0
// Empty R0:   bit 15 H = 0, bit 14 V = 0, bit 5 STL, bits 4..0 C4..C0, all else 0
// Empty R1:   0x08000
//
// T4..T0 and C4..C0 are 5-bit Gray codes of the CPLD's clock count.
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
	uint8_t		Count;		// clock count at the end of the write, mod 32 (binary)
	bool		Stall;		// empty pairs: STL, a held Z80 clock measured since the last
};

enum TPairKind
{
	PairEmpty,		// an empty pair; Count holds its clock count
	PairEvent,		// V = 1, H bits correct, fields valid
	PairMismatch,		// an H bit is wrong: Pi and CPLD disagree on the word
	PairMalformed		// H bits correct, but the pair is neither a valid
				// empty pair nor a valid event
};

static const unsigned CountBits		= 5;
static const uint32_t CountModulus	= 1u << CountBits;	// 32
static const uint32_t CountMask		= CountModulus - 1;

// Bit positions in a read word.
static const uint32_t WordH		= 1u << 15;
static const uint32_t R0V		= 1u << 14;
static const uint32_t R0Ovf		= 1u << 13;
static const uint32_t R0Frame		= 1u << 12;
static const unsigned R0KindShift	= 10;
static const uint32_t R0KindMask	= 3u << R0KindShift;
static const uint32_t R0T2		= 1u << 9;
static const uint32_t R0A15		= 1u << 8;
static const uint32_t R0DataMask	= 0xFFu;
static const uint32_t WordT4T1		= 1u << 17;	// T4 in R0, T1 in R1
static const uint32_t WordT3T0		= 1u << 16;	// T3 in R0, T0 in R1
static const uint32_t R1AddressMask	= 0x7FFFu;
static const uint32_t EmptyR0CountMask	= CountMask;
static const uint32_t EmptyR0Stall	= 1u << 5;
static const uint32_t EmptyR1		= WordH;
static const uint32_t WordMask		= 0x3FFFFu;	// 18 bits

// 5-bit reflected binary Gray code.
static inline uint8_t GrayToBinary (uint8_t nGray)
{
	uint8_t nBinary = nGray;
	for (unsigned nShift = 1; nShift < CountBits; nShift <<= 1)
	{
		nBinary ^= nBinary >> nShift;
	}
	return nBinary & CountMask;
}

static inline uint8_t BinaryToGray (uint8_t nBinary)
{
	return (nBinary ^ (nBinary >> 1)) & CountMask;
}

// TRUE when a read word can be R0 (H = 0).
static inline bool IsR0 (uint32_t nWord)
{
	return (nWord & WordH) == 0;
}

// Classifies one pair. Fills rEvent for PairEvent; for PairEmpty only
// rEvent.Count is set.
//
// Checks, in order:
//  - R0 must have H = 0 and R1 must have H = 1, otherwise PairMismatch;
//  - V = 0 gives PairEmpty only when R0 has no bit set outside H, STL and
//    C4..C0 and R1 is exactly 0x08000; otherwise PairMalformed;
//  - for V = 1: KIND 11, or a reset event with a non-zero address or DATA,
//    give PairMalformed. OVF and F may be set on any event.
// Bits above bit 17 are ignored.
static inline TPairKind DecodePair (uint32_t nR0, uint32_t nR1, TEvent &rEvent)
{
	nR0 &= WordMask;
	nR1 &= WordMask;

	if ((nR0 & WordH) != 0 || (nR1 & WordH) == 0)
	{
		return PairMismatch;
	}

	if ((nR0 & R0V) == 0)
	{
		if ((nR0 & ~(EmptyR0CountMask | EmptyR0Stall)) != 0 || nR1 != EmptyR1)
		{
			return PairMalformed;
		}
		rEvent.Count = GrayToBinary ((uint8_t) (nR0 & EmptyR0CountMask));
		rEvent.Stall = (nR0 & EmptyR0Stall) != 0;
		return PairEmpty;
	}

	uint8_t nKind = (uint8_t) ((nR0 & R0KindMask) >> R0KindShift);
	uint16_t nAddress = (uint16_t) (((nR0 & R0A15) << 7) | (nR1 & R1AddressMask));
	uint8_t nData = (uint8_t) (nR0 & R0DataMask);

	if (   nKind == KindReserved
	    || (nKind == KindReset && (nAddress != 0 || nData != 0)))
	{
		return PairMalformed;
	}

	uint8_t nGray =   ((nR0 & WordT4T1) ? 0x10 : 0)
			| ((nR0 & WordT3T0) ? 0x08 : 0)
			| ((nR0 & R0T2) ? 0x04 : 0)
			| ((nR1 & WordT4T1) ? 0x02 : 0)
			| ((nR1 & WordT3T0) ? 0x01 : 0);

	rEvent.Kind = nKind;
	rEvent.Data = nData;
	rEvent.Address = nAddress;
	rEvent.Overflow = (nR0 & R0Ovf) != 0;
	rEvent.Frame = (nR0 & R0Frame) != 0;
	rEvent.Count = GrayToBinary (nGray);
	rEvent.Stall = false;

	return PairEvent;
}

// Builds the two read words of an event, with nCount the clock count mod
// 32 (binary). Used by the host tests to make synthetic streams.
static inline void EncodeEvent (uint32_t *pWords, uint8_t nKind, uint16_t nAddress,
				uint8_t nData, uint8_t nCount, bool bOverflow = false,
				bool bFrame = false)
{
	uint8_t nGray = BinaryToGray (nCount & CountMask);

	uint32_t nR0 =   R0V | ((uint32_t) (nKind & 3) << R0KindShift)
		       | ((uint32_t) (nAddress >> 15) << 8) | nData;
	if (bOverflow)		nR0 |= R0Ovf;
	if (bFrame)		nR0 |= R0Frame;
	if (nGray & 0x10)	nR0 |= WordT4T1;
	if (nGray & 0x08)	nR0 |= WordT3T0;
	if (nGray & 0x04)	nR0 |= R0T2;

	uint32_t nR1 = WordH | (nAddress & R1AddressMask);
	if (nGray & 0x02)	nR1 |= WordT4T1;
	if (nGray & 0x01)	nR1 |= WordT3T0;

	pWords[0] = nR0;
	pWords[1] = nR1;
}

// The two read words of an empty pair with clock count nCount mod 32.
static inline void EncodeEmpty (uint32_t *pWords, uint8_t nCount, bool bStall = false)
{
	pWords[0] = BinaryToGray (nCount & CountMask) | (bStall ? EmptyR0Stall : 0);
	pWords[1] = EmptyR1;
}

}

#endif
