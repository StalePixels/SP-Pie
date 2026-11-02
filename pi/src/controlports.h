//
// controlports.h
//
// SP-Pie's control ports (docs/control-ports.md). The Spectrum writes a
// feature number to the feature select port 0x043B, then the feature's
// value to the feature data port 0x053B. Both are decoded on all 16
// address bits. The Pi sees the writes in the event stream; the CPLD
// treats them like any other I/O write.
//
//  Feature 0, SP-Pie:            no bits yet
//  Feature 1, attached devices:  bit 0 = AY, bit 1 = TurboSound,
//                                bit 2 = beeper, bit 3 = Covox on 0xFB,
//                                bit 4 = Covox on 0xDD, bit 5 = Covox on 0xDF
//  Feature 2, border:            bit 0 = border extension
//  Feature 0xFF, debug:          bit 0 = HUD (the overlay in the side areas)
//
// Other features and other bits are reserved: the data write is ignored,
// or the bit is dropped. A Spectrum reset restores the defaults and
// selects feature 0.
//
// No platform dependencies.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _sppie_controlports_h
#define _sppie_controlports_h

#include <stdint.h>

namespace sppie {

class CControlPorts
{
public:
	static const uint16_t FeatureSelectPort	= 0x043B;
	static const uint16_t FeatureDataPort	= 0x053B;

	static const uint8_t FeatureSpPie	= 0;
	static const uint8_t FeatureDevices	= 1;
	static const uint8_t FeatureBorder	= 2;
	static const uint8_t FeatureDebug	= 0xFF;

	static const uint8_t DeviceAy		= 0x01;
	static const uint8_t DeviceTurboSound	= 0x02;
	static const uint8_t DeviceBeeper	= 0x04;
	static const uint8_t DeviceCovoxFB	= 0x08;
	static const uint8_t DeviceCovoxDD	= 0x10;
	static const uint8_t DeviceCovoxDF	= 0x20;
	static const uint8_t DeviceBits		= 0x3F;
	static const uint8_t DevicesDefault	= DeviceAy | DeviceTurboSound | DeviceBeeper;

	static const uint8_t BorderExtension	= 0x01;
	static const uint8_t BorderBits		= 0x01;
	static const uint8_t BorderDefault	= 0x00;

	static const uint8_t DebugHud		= 0x01;
	static const uint8_t DebugBits		= 0x01;
	static const uint8_t DebugDefault	= DebugHud;

	CControlPorts (void)	{ Reset (); }

	// The defaults, and feature 0 selected.
	void Reset (void)
	{
		m_nFeature = FeatureSpPie;
		m_nDevices = DevicesDefault;
		m_nBorder = BorderDefault;
		m_nDebug = DebugDefault;
	}

	// An I/O write. TRUE when it was to a control port.
	bool Write (uint16_t nPort, uint8_t nData)
	{
		if (nPort == FeatureSelectPort)
		{
			m_nFeature = nData;
			return true;
		}
		if (nPort != FeatureDataPort)
		{
			return false;
		}

		switch (m_nFeature)
		{
		case FeatureDevices:
			m_nDevices = nData & DeviceBits;
			break;

		case FeatureBorder:
			m_nBorder = nData & BorderBits;
			break;

		case FeatureDebug:
			m_nDebug = nData & DebugBits;
			break;

		default:		// feature 0 and the reserved features
			break;
		}
		return true;
	}

	uint8_t Feature (void) const		{ return m_nFeature; }
	uint8_t Devices (void) const		{ return m_nDevices; }
	uint8_t Border (void) const		{ return m_nBorder; }
	uint8_t Debug (void) const		{ return m_nDebug; }
	bool Hud (void) const			{ return (m_nDebug & DebugHud) != 0; }
	bool BorderExtended (void) const	{ return (m_nBorder & BorderExtension) != 0; }

private:
	uint8_t	m_nFeature;
	uint8_t	m_nDevices;
	uint8_t	m_nBorder;
	uint8_t	m_nDebug;
};

}

#endif
