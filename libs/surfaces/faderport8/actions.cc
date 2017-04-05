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
using namespace std;

#define BindMethod(ID, CB) \
	_ctrls.button (ID).released.connect_same_thread (button_connections, boost::bind (&FaderPort8:: CB, this));

#define BindAction(ID, GRP, ITEM) \
	_ctrls.button (ID).released.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_action, this, GRP, ITEM));

void
FaderPort8::setup_actions ()
{
	BindMethod (FP8Controls::BtnPlay, button_play);
	BindMethod (FP8Controls::BtnStop, button_stop);
	BindMethod (FP8Controls::BtnLoop, button_loop);
	BindMethod (FP8Controls::BtnRecord, button_record);
	BindMethod (FP8Controls::BtnClick, button_metronom);

	BindAction (FP8Controls::BtnSave, "Common", "Save");
	BindAction (FP8Controls::BtnUndo, "Editor", "undo");
	BindAction (FP8Controls::BtnRedo, "Editor", "redo");

	_ctrls.button (FP8Controls::BtnEncoder).pressed.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_encoder, this));
}

void
FaderPort8::button_play ()
{
	if (session->transport_rolling ()) {
		transport_stop ();
	} else {
		transport_play ();
	}
}

void
FaderPort8::button_stop ()
{
	transport_stop ();
}

void
FaderPort8::button_record ()
{
	set_record_enable (!get_record_enabled ());
}

void
FaderPort8::button_loop ()
{
	loop_toggle ();
}

void
FaderPort8::button_metronom ()
{
	Config->set_clicking (!Config->get_clicking ());
}

void
FaderPort8::button_rewind ()
{
	goto_start (session->transport_rolling ());
}

void
FaderPort8::button_action (const std::string& group, const std::string& item)
{
	AccessAction (group, item);
}

void
FaderPort8::button_encoder ()
{
	switch (_ctrls.nav_mode()) {
		case FP8Controls::NavChannel:
			break;
		case FP8Controls::NavZoom:
			ZoomToSession ();
			break;
		case FP8Controls::NavScroll:
			break;
		case FP8Controls::NavBank:
			break;
		case FP8Controls::NavMaster:
			break;
		case FP8Controls::NavSection:
			break;
		case FP8Controls::NavMarker:
			{
				string markername;
				/* Don't add another mark if one exists within 1/100th of a second of
				 * the current position and we're not rolling.
				 */
				framepos_t where = session->audible_frame();
				if (session->transport_stopped() && session->locations()->mark_at (where, session->frame_rate() / 100.0)) {
					return;
				}

				session->locations()->next_available_name (markername,"mark");
				add_marker (markername);
			}
			break;
	}
}

void
FaderPort8::encoder_navigate (bool neg, int steps)
{
	switch (_ctrls.nav_mode()) {
		case FP8Controls::NavChannel:
			if (neg) {
				StepTracksUp ();
			} else {
				StepTracksDown ();
			}
			break;
		case FP8Controls::NavZoom:
			if (neg) {
				ZoomOut ();
			} else {
				ZoomIn ();
			}
			break;
		case FP8Controls::NavScroll:
			ScrollTimeline ((neg ? -.05 : .05) * steps);
			break;
		case FP8Controls::NavBank:
			break;
		case FP8Controls::NavMaster:
			break;
		case FP8Controls::NavSection:
			break;
		case FP8Controls::NavMarker:
			if (neg) {
				prev_marker ();
			} else {
				next_marker ();
			}
			break;
	}
}
