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

#include "ardour/session.h"
#include "ardour/session_configuration.h"

#include "gtkmm2ext/actions.h"

#include "faderport8.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourSurface;

void
FaderPort8::connect_session_signals()
{
	Config->ParameterChanged.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_parameter_changed, this, _1), this);
	session->config.ParameterChanged.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_parameter_changed, this, _1), this);

	/* Session Signals -- TODO use some Macro */
	session->TransportStateChange.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_transport_state_changed, this), this);
	session->TransportLooped.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_loop_state_changed, this), this);
	session->RecordStateChanged.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_record_state_changed, this), this);

	session->DirtyChanged.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_session_dirty_changed, this), this);
	session->history().Changed.connect (session_connections, MISSING_INVALIDATOR, boost::bind (&FaderPort8::notify_history_changed, this), this);

}

void
FaderPort8::send_session_state ()
{
	notify_transport_state_changed ();
	notify_record_state_changed ();
	notify_session_dirty_changed ();
	notify_history_changed ();
	notify_parameter_changed ("clicking");
}

void
FaderPort8::notify_parameter_changed (std::string param)
{
	if (param == "clicking") {
		_ctrls.button (FP8Controls::BtnClick).set_active (Config->get_clicking ());
	}
}

void
FaderPort8::notify_transport_state_changed ()
{
	if (session->transport_rolling ()) {
		_ctrls.button (FP8Controls::BtnPlay).set_active (true);
		_ctrls.button (FP8Controls::BtnStop).set_active (false);
	} else {
		_ctrls.button (FP8Controls::BtnPlay).set_active (false);
		_ctrls.button (FP8Controls::BtnStop).set_active (true);
	}
	notify_loop_state_changed ();
}

void
FaderPort8::notify_record_state_changed ()
{
	switch (session->record_status ()) {
		case Session::Disabled:
			_ctrls.button (FP8Controls::BtnRecord).set_active (0);
			_ctrls.button (FP8Controls::BtnRecord).set_blinking (false);
			break;
		case Session::Enabled:
			_ctrls.button (FP8Controls::BtnRecord).set_active (true);
			_ctrls.button (FP8Controls::BtnRecord).set_blinking (true);
			break;
		case Session::Recording:
			_ctrls.button (FP8Controls::BtnRecord).set_active (true);
			_ctrls.button (FP8Controls::BtnRecord).set_blinking (false);
			break;
	}
}

void
FaderPort8::notify_loop_state_changed ()
{
	bool looping = false;
	Location* looploc = session->locations ()->auto_loop_location ();
	if (looploc && session->get_play_loop ()) {
		looping = true;
	}
	_ctrls.button (FP8Controls::BtnLoop).set_active (looping);
}

void
FaderPort8::notify_session_dirty_changed ()
{
	const bool is_dirty = session->dirty ();
	_ctrls.button (FP8Controls::BtnSave).set_active (is_dirty);
	_ctrls.button (FP8Controls::BtnSave).set_color (is_dirty ? 0xff0000ff : 0x00ff00ff);
}

void
FaderPort8::notify_history_changed ()
{
	_ctrls.button (FP8Controls::BtnRedo).set_active (session->redo_depth() > 0);
	_ctrls.button (FP8Controls::BtnUndo).set_active (session->undo_depth() > 0);
}
