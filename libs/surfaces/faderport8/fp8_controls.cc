/*
 * Copyright (C) 2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "fp8_controls.h"

using namespace ArdourSurface;

#define NEWBUTTON(midi_id, button_id, color)            \
  do {                                                  \
  assert (_midimap.end() == _midimap.find (midi_id));   \
  assert (_ctrlmap.end() == _ctrlmap.find (button_id)); \
  FP8Button *t = new FP8Button (b, midi_id);            \
  _midimap[midi_id] = t;                                \
  _ctrlmap[button_id] = t;                              \
  } while (0)


#define NEWSHIFTBUTTON(midi_id, id1, id2, color)        \
  do {                                                  \
  assert (_midimap.end() == _midimap.find (midi_id));   \
  assert (_ctrlmap.end() == _ctrlmap.find (id1));       \
  assert (_ctrlmap.end() == _ctrlmap.find (id2));       \
  FP8ShiftSensitiveButton *t =                          \
    new FP8ShiftSensitiveButton (b, midi_id, color);    \
  _midimap[midi_id] = t;                                \
  _ctrlmap[id1] = t->button ();                         \
  _ctrlmap[id2] = t->button_shift ();                   \
  } while (0)


FP8Controls::FP8Controls (FP8Base& b)
	: _fadermode (ModeTrack)
	, _navmode (NavMaster)
{
	NEWBUTTON (0x56, BtnLoop, false);
	NEWBUTTON (0x5b, BtnRewind, false);
	NEWBUTTON (0x5c, BtnFastForward, false);
	NEWBUTTON (0x5d, BtnStop, false);
	NEWBUTTON (0x5e, BtnPlay, false);
	NEWBUTTON (0x5f, BtnRecord, false);

	NEWSHIFTBUTTON (0x4a, BtnARead, BtnUser3, true);
	NEWSHIFTBUTTON (0x4b, BtnAWrite, BtnUser2, true);
	NEWSHIFTBUTTON (0x4c, BtnATrim, BtnRedo, true);
	NEWSHIFTBUTTON (0x4d, BtnATouch, BtnUser1, true);
	NEWSHIFTBUTTON (0x4e, BtnALatch, BtnSave, true);
	NEWSHIFTBUTTON (0x4f, BtnAOff, BtnUndo, true);

	NEWBUTTON (0x2e, BtnPrev, false);
	NEWBUTTON (0x2f, BtnNext, false);
	NEWBUTTON (0x53, BtnEncoder, false); // XXX no feedback

	NEWSHIFTBUTTON (0x36, BtnChannel, BtnF1, false);
	NEWSHIFTBUTTON (0x37, BtnZoom,    BtnF2, false);
	NEWSHIFTBUTTON (0x38, BtnScroll,  BtnF3, false);
	NEWSHIFTBUTTON (0x39, BtnBank,    BtnF4, false);
	NEWSHIFTBUTTON (0x3a, BtnMaster,  BtnF5, false);
	NEWSHIFTBUTTON (0x3b, BtnClick,   BtnF6, false);
	NEWSHIFTBUTTON (0x3c, BtnSection, BtnF7, false);
	NEWSHIFTBUTTON (0x3d, BtnMarker,  BtnF8, false);

	NEWSHIFTBUTTON (0x28, BtnTrack, BtnTimecode, false);
	NEWBUTTON (0x2b, BtnPlugins, false);
	NEWBUTTON (0x29, BtnSend, false);
	NEWBUTTON (0x2a, BtnPan, false);

	NEWSHIFTBUTTON (0x00, BtnArm, BtnArmAll, false);
	NEWBUTTON (0x01, BtnSoloClear, false);
	NEWBUTTON (0x02, BtnMuteClear, false);

	NEWSHIFTBUTTON (0x03, BtnBypass, BtnBypassAll, true);
	NEWSHIFTBUTTON (0x04, BtnMacro, BtnOpen, true);
	NEWSHIFTBUTTON (0x05, BtnLock, BtnLink, true);

	NEWSHIFTBUTTON (0x3e, BtnMAudio, BtnMInputs, true);
	NEWSHIFTBUTTON (0x3f, BtnMVI, BtnMMIDI, true);
	NEWSHIFTBUTTON (0x40, BtnMBus, BtnMOutputs, true);
	NEWSHIFTBUTTON (0x41, BtnMVCA, BtnMFX, true);
	NEWSHIFTBUTTON (0x42, BtnMAll, BtnMUser, true);

#define BindNav(BTN, MODE)\
	button (BTN).pressed.connect_same_thread (button_connections, boost::bind (&FP8Controls::set_nav_mode, this, MODE))

	BindNav (BtnChannel, NavChannel);
	BindNav (BtnZoom,    NavZoom);
	BindNav (BtnScroll,  NavScroll);
	BindNav (BtnBank,    NavBank);
	BindNav (BtnMaster,  NavMaster);
	BindNav (BtnSection, NavSection);
	BindNav (BtnMarker,  NavMarker);

#define BindFader(BTN, MODE)\
	button (BTN).released.connect_same_thread (button_connections, boost::bind (&FP8Controls::set_fader_mode, this, MODE))

	BindFader (BtnTrack,   ModeTrack);
	BindFader (BtnPlugins, ModePlugins);
	BindFader (BtnSend,    ModeSend);
	BindFader (BtnPan,     ModePan);

	/* create channelstrips */
	for (uint8_t id = 0; id < 8; ++id) {
		chanstrip[id] = new FP8Strip (b, id);
		_midimap_strip[0x08 + id] = &(chanstrip[id]->solo_button());
		_midimap_strip[0x10 + id] = &(chanstrip[id]->mute_button());
		_midimap_strip[0x18 + id] = &(chanstrip[id]->select_button());
	}
}

FP8Controls::~FP8Controls ()
{
	for (MidiButtonMap::const_iterator i = _midimap.begin (); i != _midimap.end (); ++i) {
		delete i->second;
	}
	for (uint8_t id = 0; id < 8; ++id) {
		delete chanstrip[id];
	}
	_midimap_strip.clear ();
	_ctrlmap.clear ();
	_midimap.clear ();
}

void
FP8Controls::initialize ()
{
	/* set RGM colors */
	button (BtnUndo).set_color (0x00ff00ff);
	button (BtnRedo).set_color (0x00ff00ff);

	button (BtnAOff).set_color (0xffffffff);
	button (BtnATrim).set_color (0x000030ff);
	button (BtnARead).set_color (0x00ff00ff);
	button (BtnAWrite).set_color (0xff0000ff);
	button (BtnATouch).set_color (0xff8800ff);

	button (BtnUser1).set_color (0x0000ffff);
	button (BtnUser2).set_color (0x0000ffff);
	button (BtnUser3).set_color (0x0000ffff);

	button (BtnALatch).set_color (0x0000ffff);

	button (BtnBypass).set_color (0xff0000ff);
	button (BtnBypassAll).set_color (0xff8800ff);

	button (BtnMacro).set_color (0xff0000ff);
	button (BtnOpen).set_color (0xff8800ff);

	button (BtnLink).set_color (0xff0000ff);
	button (BtnLock).set_color (0xff8800ff);

	g_usleep (10000);

	button (BtnMAudio).set_color (0x0000ffff);
	button (BtnMVI).set_color (0x0000ffff);
	button (BtnMBus).set_color (0x0000ffff);
	button (BtnMVCA).set_color (0x0000ffff);
	button (BtnMAll).set_color (0x0000ffff);

	button (BtnMInputs).set_color (0x0000ffff);
	button (BtnMMIDI).set_color (0x0000ffff);
	button (BtnMOutputs).set_color (0x0000ffff);
	button (BtnMFX).set_color (0x0000ffff);
	button (BtnMUser).set_color (0x0000ffff);

	g_usleep (10000);

	for (uint8_t id = 0; id < 8; ++id) {
		chanstrip[id]->select_button().set_color (0xffffffff);
	}

	g_usleep (10000);

	/* initally turn all lights off */
	for (CtrlButtonMap::const_iterator i = _ctrlmap.begin (); i != _ctrlmap.end (); ++i) {
		i->second->set_active (false);
		g_usleep (1000);
	}

	/* default modes */
	button (BtnMaster).set_active (true);
	button (BtnTrack).set_active (true);
}

FP8ButtonInterface&
FP8Controls::button (ButtonId id)
{
	CtrlButtonMap::const_iterator i = _ctrlmap.find (id);
	if (i == _ctrlmap.end()) {
		return _dummy_button;
	}
	return *(i->second);
}

FP8Strip&
FP8Controls::strip (uint8_t id)
{
	assert (id < 8);
	return *chanstrip[id];
}

bool
FP8Controls::midi_event (uint8_t id, uint8_t val)
{
	MidiButtonMap::const_iterator i = _midimap_strip.find (id);
	if (i != _midimap_strip.end()) {
		return i->second->midi_event (val > 0x40);
	}

	i = _midimap.find (id);
	if (i != _midimap.end()) {
		return i->second->midi_event (val > 0x40);
	}
	return false;
}

bool
FP8Controls::midi_touch (uint8_t id, uint8_t val)
{
	assert (id < 8);
	return chanstrip[id]->midi_touch (val > 0x40);
}

bool
FP8Controls::midi_fader (uint8_t id, unsigned short val)
{
	assert (id < 8);
	return chanstrip[id]->midi_fader ((val >> 4) / 1023.f);
}

void
FP8Controls::set_nav_mode (NavigationMode m)
{
	if (_navmode == m) {
		return;
	}
	// TODO add special-cases
	// master/monitor
	// "click" hold -> set click volume
	button (BtnChannel).set_active (m == NavChannel);
	button (BtnZoom).set_active (m == NavZoom);
	button (BtnScroll).set_active (m == NavScroll);
	button (BtnBank).set_active (m == NavBank);
	button (BtnMaster).set_active (m == NavMaster);
	button (BtnSection).set_active (m == NavSection);
	button (BtnMarker).set_active (m == NavMarker);
	_navmode = m;
}

void
FP8Controls::set_fader_mode (FaderMode m)
{
	if (_fadermode == m) {
		return;
	}
	button (BtnTrack).set_active (m == ModeTrack);
	button (BtnPlugins).set_active (m == ModePlugins);
	button (BtnSend).set_active (m == ModeSend);
	button (BtnPan).set_active (m == ModePan);
	_fadermode = m;
	FaderModeChanged ();
}
