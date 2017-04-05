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

#include "ardour/gain_control.h"
#include "ardour/meter.h"
#include "ardour/mute_control.h"
#include "ardour/session.h"
#include "ardour/solo_control.h"
#include "ardour/stripable.h"

#include "fp8_strip.h"

using namespace ArdourSurface;
using namespace ARDOUR;

FP8Strip::FP8Strip (FP8Base& b, uint8_t id)
	: _base (b)
	, _id (id)
	, _solo   (b, 0x08 + id)
	, _mute   (b, 0x10 + id)
	, _select (b, 0x18 + id, true)
	, _touching (false)
	, _last_gain (65535)
{
	assert (id < 8);
	_mute.StateChange.connect_same_thread (_button_connections, boost::bind (&FP8Strip::set_mute, this, _1));
	_solo.StateChange.connect_same_thread (_button_connections, boost::bind (&FP8Strip::set_solo, this, _1));
	b.Periodic.connect_same_thread (_base_connection, boost::bind (&FP8Strip::periodic, this));
}

FP8Strip::~FP8Strip ()
{
	_stripable_connections.drop_connections ();
}

bool
FP8Strip::midi_touch (bool t)
{
	_touching = t;
	if (!_stripable) {
		return false;
	}
	boost::shared_ptr<GainControl> gc = _stripable->gain_control();
	if (t) {
		gc->start_touch(gc->session().transport_frame());
	} else {
		gc->stop_touch(true, gc->session().transport_frame());
	}
	return true;
}

bool
FP8Strip::midi_fader (float val)
{
	assert (val >= 0.f && val <= 1.f);
	if (!_stripable) {
		return false;
	}
	boost::shared_ptr<GainControl> gc = _stripable->gain_control();
	gc->set_value (gc->interface_to_internal (val), PBD::Controllable::UseGroup);
	return true;
}

void
FP8Strip::set_stripable (boost::shared_ptr<Stripable> s)
{
	_stripable = s;
	_solo.reset ();
	_mute.reset ();
	_last_gain = 65535;

	g_usleep (5000);

	if (!s) {
		_stripable_connections.drop_connections ();
		_solo.set_active (false);
		_mute.set_active (false);
		_select.set_active (false);
		_select.set_color (0xffffffff);
		_base.tx_sysex (3, 0x13, _id, 0x10); // clear, + mode 0: 3 lines of text + value
		_base.tx_sysex (4, 0x12, _id, 0x00, 0x00);
		return;
	}

	_base.tx_midi3 (0xb0, 0x38 + _id, 0x01); // value-bar mode

	_base.tx_sysex (3, 0x13, _id, 0x15); // clear, + mode 4: 3 lines of text, meters + value
	_base.tx_text (_id, 0x00, 0x00, s->name());
	g_usleep (5000);

	_select.set_active (s->is_selected ());
	_select.set_color (s->presentation_info ().color());

	s->solo_control()->Changed.connect (_stripable_connections, MISSING_INVALIDATOR, boost::bind (&FP8Strip::solo_changed, this), dynamic_cast<BaseUI*>(&_base));
	s->mute_control()->Changed.connect (_stripable_connections, MISSING_INVALIDATOR, boost::bind (&FP8Strip::mute_changed, this), dynamic_cast<BaseUI*>(&_base));
	s->gain_control()->Changed.connect (_stripable_connections, MISSING_INVALIDATOR, boost::bind (&FP8Strip::gain_changed, this), dynamic_cast<BaseUI*>(&_base));

	gain_changed ();
	mute_changed ();
	solo_changed ();
}

void
FP8Strip::set_mute (bool on)
{
	if (_stripable) {
		_stripable->mute_control()->set_value (on ? 1.0 : 0.0, PBD::Controllable::UseGroup);
	}
}

void
FP8Strip::set_solo (bool on)
{
	if (_stripable) {
		_stripable->solo_control()->set_value (on ? 1.0 : 0.0, PBD::Controllable::UseGroup);
	}
}

void
FP8Strip::solo_changed ()
{
	if (_stripable) {
		_solo.set_active (_stripable->solo_control()->self_soloed());
	}
}

void
FP8Strip::mute_changed ()
{
	if (_stripable) {
		_mute.set_active (_stripable->mute_control()->muted());
	}
}

void
FP8Strip::gain_changed ()
{
	if (!_stripable || _touching) {
		return;
	}
	boost::shared_ptr<GainControl> gc = _stripable->gain_control();
	float val = gc->internal_to_interface (gc->get_value()) * 16368.f; /* 16 * 1023 */
	unsigned short mv = lrintf (val);
	if (mv == _last_gain) {
		return;
	}
	_last_gain = mv;
	_base.tx_midi3 (0xe0 + _id, (mv & 0x7f), (mv >> 7) & 0x7f);
}

void
FP8Strip::update_fader ()
{
	if (!_stripable || _touching) {
		return;
	}
	ARDOUR::AutoState state = _stripable->gain_control()->automation_state();
	if (state == Touch || state == Play) {
		gain_changed ();
	}
}

void
FP8Strip::update_meter ()
{
	if (!_stripable) {
		return;
	}
	// TODO  send these only when changed

	float dB = _stripable->peak_meter()->meter_level (0, MeterMCP);
	// TODO: deflect
	int val = std::min (127.f, std::max (0.f, 2.f * dB + 127.f));
	// gain meter
	_base.tx_midi2 (0xd0 + _id, val & 0x7f); // falls off automatically
	// gain reduction
	_base.tx_midi2 (0xd8 + _id, val & 0x7f);

	boost::shared_ptr<AutomationControl> pan_control =
		_stripable->pan_azimuth_control ();
	float panpos = pan_control->internal_to_interface (pan_control->get_value());
	val = std::min (127.f, std::max (0.f, panpos * 128.f));
	_base.tx_midi3 (0xb0, 0x30 + _id, val & 0x7f);
}

void
FP8Strip::periodic ()
{
	update_fader ();
	update_meter ();
}
