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

#ifndef _ardour_surfaces_fp8strip_h_
#define _ardour_surfaces_fp8strip_h_

#include <stdint.h>
#include <boost/shared_ptr.hpp>

#include "pbd/signals.h"

#include "fp8_base.h"
#include "fp8_button.h"

namespace ARDOUR {
	class Stripable;
}

namespace ArdourSurface {

class FP8Strip
{
public:
	FP8Strip (FP8Base& b, uint8_t id);
	~FP8Strip ();

	FP8ButtonInterface& solo_button () { return _solo; }
	FP8ButtonInterface& mute_button () { return _mute; }
	FP8ButtonInterface& select_button () { return _select; }

	bool midi_touch (bool t);
	bool midi_fader (float val);

	void set_stripable (boost::shared_ptr<ARDOUR::Stripable>);

private:
	FP8Base&  _base;
	uint8_t   _id;
	FP8MomentaryButton _solo;
	FP8MomentaryButton _mute;
	FP8Button _select;

	bool _touching;
	unsigned short _last_gain;

	PBD::ScopedConnection _base_connection;
	PBD::ScopedConnectionList _button_connections;
	PBD::ScopedConnectionList _stripable_connections;

	boost::shared_ptr<ARDOUR::Stripable> _stripable;

	void set_mute (bool);
	void set_solo (bool);

	void gain_changed ();
	void solo_changed ();
	void mute_changed ();

	void update_fader ();
	void update_meter ();
	void periodic ();
};

} /* namespace */
#endif /* _ardour_surfaces_fp8strip_h_ */

