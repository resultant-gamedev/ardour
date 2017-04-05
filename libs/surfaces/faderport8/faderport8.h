/*
 * Copyright (C) 2017 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2015 Paul Davis
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

#ifndef ardour_surface_faderport8_h
#define ardour_surface_faderport8_h

#include <list>
#include <map>
#include <glibmm/threads.h>

#define ABSTRACT_UI_EXPORTS
#include "pbd/abstract_ui.h"

#include "ardour/types.h"
#include "ardour/async_midi_port.h"
#include "ardour/midi_port.h"

#include "control_protocol/control_protocol.h"

#include "fp8_base.h"
#include "fp8_controls.h"

namespace MIDI {
	class Parser;
}

namespace ARDOUR {
	class Bundle;
	class Session;
}

namespace ArdourSurface {

struct FaderPort8Request : public BaseUI::BaseRequestObject
{
	public:
		FaderPort8Request () {}
		~FaderPort8Request () {}
};

class FaderPort8 : public FP8Base, public ARDOUR::ControlProtocol, public AbstractUI<FaderPort8Request>
{
public:
	FaderPort8 (ARDOUR::Session&);
	virtual ~FaderPort8();

	int set_active (bool yn);

	/* we probe for a device when our ports are connected. Before that,
	 * there's no way to know if the device exists or not.
	 */
	static bool  probe() { return true; }
	static void* request_factory (uint32_t);

	XMLNode& get_state ();
	int set_state (const XMLNode&, int version);

	/* configuration GUI */
	bool  has_editor () const { return true; }
	void* get_gui () const;
	void  tear_down_gui ();

	int stop ();
	void do_request (FaderPort8Request*);
	void thread_init ();

	PBD::Signal0<void> ConnectionChange;

	boost::shared_ptr<ARDOUR::Port> input_port() const { return _input_port; }
	boost::shared_ptr<ARDOUR::Port> output_port() const { return _output_port; }
	std::list<boost::shared_ptr<ARDOUR::Bundle> > bundles ();

	size_t tx_midi (std::vector<uint8_t> const&) const;

private:
	void close ();

	void start_midi_handling ();
	void stop_midi_handling ();

	boost::shared_ptr<ARDOUR::AsyncMIDIPort> _input_port;
	boost::shared_ptr<ARDOUR::AsyncMIDIPort> _output_port;
	boost::shared_ptr<ARDOUR::Bundle> _input_bundle;
	boost::shared_ptr<ARDOUR::Bundle> _output_bundle;

	PBD::ScopedConnectionList session_connections;
	PBD::ScopedConnectionList button_connections;
	PBD::ScopedConnectionList midi_connections;

	bool midi_input_handler (Glib::IOCondition ioc, boost::weak_ptr<ARDOUR::AsyncMIDIPort> port);

	bool connection_handler (boost::weak_ptr<ARDOUR::Port>, std::string name1, boost::weak_ptr<ARDOUR::Port>, std::string name2, bool yn);
	PBD::ScopedConnection port_connection;

	enum ConnectionState {
		InputConnected = 0x1,
		OutputConnected = 0x2
	};

	void connected ();
	int  _connection_state;
	bool _device_active;

	void sysex_handler (MIDI::Parser &p, MIDI::byte *, size_t);
	void polypressure_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void pitchbend_handler (MIDI::Parser &, uint8_t chan, MIDI::pitchbend_t pb);
	void controller_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void note_on_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void note_off_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb);

	FP8Controls _ctrls;

	Glib::RefPtr<Glib::TimeoutSource> periodic_timer;
	sigc::connection _periodic_connection;
	bool periodic ()
	{
		Periodic ();
		return true;
	}

	Glib::RefPtr<Glib::TimeoutSource> blink_timer;
	sigc::connection _blink_connection;
	bool _blink_onoff;

	bool blink_it ()
	{
		_blink_onoff = !_blink_onoff;
		BlinkIt (_blink_onoff);
		return true;
	}

	/* shift key */
	sigc::connection _shift_connection;
	bool _shift_lock;
	bool shift_timeout () { _shift_lock = true; return false; }

	/* GUI */
	void build_gui ();
	mutable void *gui;

	/* setup callbacks & actions */
	void connect_session_signals ();
	void setup_actions ();
	void send_session_state ();

	/* callbacks */
	void notify_parameter_changed (std::string);

	void notify_record_state_changed ();
	void notify_transport_state_changed ();
	void notify_loop_state_changed ();
	void notify_snap_change ();
	void notify_session_dirty_changed ();
	void notify_history_changed ();

	/* actions */
	void button_play ();
	void button_stop ();
	void button_record ();
	void button_loop ();
	void button_rewind ();
	void button_metronom ();

	void button_action (const std::string& group, const std::string& item);
	void button_encoder ();
	void encoder_navigate (bool, int);
};

} /* namespace */

#endif /* ardour_surface_faderport8_h */
