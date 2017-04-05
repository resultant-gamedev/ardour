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

#include <cstdlib>
#include <sstream>
#include <algorithm>

#include <stdint.h>

#include "pbd/error.h"
#include "pbd/failed_constructor.h"
#include "pbd/pthread_utils.h"
#include "pbd/compose.h"
#include "pbd/xml++.h"

#include "midi++/port.h"

#include "ardour/audioengine.h"
#include "ardour/bundle.h"
#include "ardour/debug.h"
#include "ardour/midiport_manager.h"
#include "ardour/rc_configuration.h"
#include "ardour/session.h"
#include "ardour/session_configuration.h"

#include "faderport8.h"

using namespace ARDOUR;
using namespace ArdourSurface;
using namespace PBD;
using namespace Glib;
using namespace std;

#include "pbd/i18n.h"

#include "pbd/abstract_ui.cc" // instantiate template

#ifndef NDEBUG
//#define VERBOSE_DEBUG
#endif

static void
debug_2byte_msg (std::string const& msg, int b0, int b1)
{
#ifndef NDEBUG
	if (DEBUG_ENABLED(DEBUG::FaderPort8)) {
		DEBUG_STR_DECL(a);
		DEBUG_STR_APPEND(a, "RECV: ");
		DEBUG_STR_APPEND(a, msg);
		DEBUG_STR_APPEND(a,' ');
		DEBUG_STR_APPEND(a,hex);
		DEBUG_STR_APPEND(a,"0x");
		DEBUG_STR_APPEND(a, b0);
		DEBUG_STR_APPEND(a,' ');
		DEBUG_STR_APPEND(a,"0x");
		DEBUG_STR_APPEND(a, b1);
		DEBUG_STR_APPEND(a,'\n');
		DEBUG_TRACE (DEBUG::FaderPort8, DEBUG_STR(a).str());
	}
#endif
}

FaderPort8::FaderPort8 (Session& s)
	: ControlProtocol (s, _("PreSonus FaderPort8"))
	, AbstractUI<FaderPort8Request> (name())
	, _connection_state (ConnectionState (0))
	, _device_active (false)
	, _ctrls (*this)
	, _blink_onoff (false)
	, _shift_lock (false)
	, gui (0)
{
	boost::shared_ptr<ARDOUR::Port> inp;
	boost::shared_ptr<ARDOUR::Port> outp;

	inp  = AudioEngine::instance()->register_input_port (DataType::MIDI, "Faderport Recv", true);
	outp = AudioEngine::instance()->register_output_port (DataType::MIDI, "Faderport Send", true);
	_input_port = boost::dynamic_pointer_cast<AsyncMIDIPort>(inp);
	_output_port = boost::dynamic_pointer_cast<AsyncMIDIPort>(outp);

	if (_input_port == 0 || _output_port == 0) {
		throw failed_constructor();
	}

	_input_bundle.reset (new ARDOUR::Bundle (_("Faderport8 (Receive)"), true));
	_output_bundle.reset (new ARDOUR::Bundle (_("Faderport8 (Send) "), false));

	_input_bundle->add_channel (
		inp->name(),
		ARDOUR::DataType::MIDI,
		session->engine().make_port_name_non_relative (inp->name())
		);

	_output_bundle->add_channel (
		outp->name(),
		ARDOUR::DataType::MIDI,
		session->engine().make_port_name_non_relative (outp->name())
		);

	ARDOUR::AudioEngine::instance()->PortConnectedOrDisconnected.connect (port_connection, MISSING_INVALIDATOR, boost::bind (&FaderPort8::connection_handler, this, _1, _2, _3, _4, _5), this);

	setup_actions ();
}

FaderPort8::~FaderPort8 ()
{
	cerr << "~FP8\n";
	_periodic_connection.disconnect ();
	_blink_connection.disconnect ();

	if (_input_port) {
		DEBUG_TRACE (DEBUG::FaderPort8, string_compose ("unregistering input port %1\n", boost::shared_ptr<ARDOUR::Port>(_input_port)->name()));
		AudioEngine::instance()->unregister_port (_input_port);
		_input_port.reset ();
	}

	if (_output_port) {
		_output_port->drain (10000,  250000); /* check every 10 msecs, wait up to 1/4 second for the port to drain */
		DEBUG_TRACE (DEBUG::FaderPort8, string_compose ("unregistering output port %1\n", boost::shared_ptr<ARDOUR::Port>(_output_port)->name()));
		AudioEngine::instance()->unregister_port (_output_port);
		_output_port.reset ();
	}

	tear_down_gui ();

	/* stop event loop */
	DEBUG_TRACE (DEBUG::FaderPort8, "BaseUI::quit ()\n");
	BaseUI::quit ();
}

/* ****************************************************************************
 * Event Loop
 */

void*
FaderPort8::request_factory (uint32_t num_requests)
{
	/* AbstractUI<T>::request_buffer_factory() is a template method only
	 * instantiated in this source module. To provide something visible for
	 * use in the interface/descriptor, we have this static method that is
	 * template-free.
	 */
	return request_buffer_factory (num_requests);
}

void
FaderPort8::do_request (FaderPort8Request* req)
{
	if (req->type == CallSlot) {
		call_slot (MISSING_INVALIDATOR, req->the_slot);
	} else if (req->type == Quit) {
		stop ();
	}
}

int
FaderPort8::stop ()
{
	BaseUI::quit ();
	return 0;
}

void
FaderPort8::thread_init ()
{
	struct sched_param rtparam;

	pthread_set_name (event_loop_name().c_str());

	PBD::notify_event_loops_about_thread_creation (pthread_self(), event_loop_name(), 2048);
	ARDOUR::SessionEvent::create_per_thread_pool (event_loop_name(), 128);

	memset (&rtparam, 0, sizeof (rtparam));
	rtparam.sched_priority = 9; /* XXX should be relative to audio (JACK) thread */

	if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &rtparam) != 0) {
		// do we care? not particularly.
	}

	blink_timer = Glib::TimeoutSource::create (200);
	periodic_timer = Glib::TimeoutSource::create (100);

}

/* ****************************************************************************
 * Port and Signal Connection Management
 */
int
FaderPort8::set_active (bool yn)
{
	DEBUG_TRACE (DEBUG::FaderPort8, string_compose("Faderport::set_active init with yn: '%1'\n", yn));

	if (yn == active()) {
		return 0;
	}

	if (yn) {
		/* start event loop */
		BaseUI::run ();
		connect_session_signals ();
	} else {
		BaseUI::quit ();
		close ();
	}

	ControlProtocol::set_active (yn);
	DEBUG_TRACE (DEBUG::FaderPort8, string_compose("Faderport::set_active done with yn: '%1'\n", yn));
	return 0;
}

void
FaderPort8::close ()
{
	stop_midi_handling ();
	session_connections.drop_connections ();
	port_connection.disconnect ();
}

void
FaderPort8::stop_midi_handling ()
{
	_periodic_connection.disconnect ();
	_blink_connection.disconnect ();

	midi_connections.drop_connections ();
	/* Note: the input handler is still active at this point, but we're no
	 * longer connected to any of the parser signals
	 */
}

void
FaderPort8::connected ()
{
	DEBUG_TRACE (DEBUG::FaderPort8, "initializing\n");
	// TODO check firmware version..
	start_midi_handling ();
	g_usleep (50000); // Flush
	_ctrls.initialize ();
	send_session_state ();

#if 1 // XXX quick test..
	uint8_t id = 0;
	boost::shared_ptr<RouteList> rl = session->get_routes();
	for (RouteList::const_iterator r = rl->begin(); r != rl->end(); ++r, ++id) {
		if (id == 8) {
			break;
		}
		_ctrls.strip(id).set_stripable (*r);
	}
#endif

	_blink_connection = blink_timer->connect (sigc::mem_fun (*this, &FaderPort8::blink_it));
	blink_timer->attach (main_loop()->get_context());

	_periodic_connection = periodic_timer->connect (sigc::mem_fun (*this, &FaderPort8::periodic));
	periodic_timer->attach (main_loop()->get_context());
}

bool
FaderPort8::connection_handler (boost::weak_ptr<ARDOUR::Port>, std::string name1, boost::weak_ptr<ARDOUR::Port>, std::string name2, bool yn)
{
#ifdef VERBOSE_DEBUG
	DEBUG_TRACE (DEBUG::FaderPort8, "FaderPort8::connection_handler: start\n");
#endif
	if (!_input_port || !_output_port) {
		return false;
	}

	string ni = ARDOUR::AudioEngine::instance()->make_port_name_non_relative (boost::shared_ptr<ARDOUR::Port>(_input_port)->name());
	string no = ARDOUR::AudioEngine::instance()->make_port_name_non_relative (boost::shared_ptr<ARDOUR::Port>(_output_port)->name());

	if (ni == name1 || ni == name2) {
		if (yn) {
			_connection_state |= InputConnected;
		} else {
			_connection_state &= ~InputConnected;
		}
	} else if (no == name1 || no == name2) {
		if (yn) {
			_connection_state |= OutputConnected;
		} else {
			_connection_state &= ~OutputConnected;
		}
	} else {
#ifdef VERBOSE_DEBUG
		DEBUG_TRACE (DEBUG::FaderPort8, string_compose ("Connections between %1 and %2 changed, but I ignored it\n", name1, name2));
#endif
		/* not our ports */
		return false;
	}

	if ((_connection_state & (InputConnected|OutputConnected)) == (InputConnected|OutputConnected)) {

		/* XXX this is a horrible hack. Without a short sleep here,
		 * something prevents the device wakeup messages from being
		 * sent and/or the responses from being received.
		 */
		g_usleep (100000);
		DEBUG_TRACE (DEBUG::FaderPort8, "device now connected for both input and output\n");
		connected ();

	} else {
		DEBUG_TRACE (DEBUG::FaderPort8, "Device disconnected (input or output or both) or not yet fully connected\n");
		_device_active = false;
	}

	ConnectionChange (); /* emit signal for our GUI */

#ifdef VERBOSE_DEBUG
	DEBUG_TRACE (DEBUG::FaderPort8, "FaderPort8::connection_handler: end\n");
#endif

	return true; /* connection status changed */
}

list<boost::shared_ptr<ARDOUR::Bundle> >
FaderPort8::bundles ()
{
	list<boost::shared_ptr<ARDOUR::Bundle> > b;

	if (_input_bundle) {
		b.push_back (_input_bundle);
		b.push_back (_output_bundle);
	}

	return b;
}

/* ****************************************************************************
 * MIDI I/O
 */
bool
FaderPort8::midi_input_handler (Glib::IOCondition ioc, boost::weak_ptr<ARDOUR::AsyncMIDIPort> wport)
{
	boost::shared_ptr<AsyncMIDIPort> port (wport.lock());

	if (!port) {
		return false;
	}

#ifdef VERBOSE_DEBUG
	DEBUG_TRACE (DEBUG::FaderPort8, string_compose ("something happend on %1\n", boost::shared_ptr<MIDI::Port>(port)->name()));
#endif

	if (ioc & ~IO_IN) {
		return false;
	}

	if (ioc & IO_IN) {

		port->clear ();
#ifdef VERBOSE_DEBUG
		DEBUG_TRACE (DEBUG::FaderPort8, string_compose ("data available on %1\n", boost::shared_ptr<MIDI::Port>(port)->name()));
#endif
		framepos_t now = session->engine().sample_time();
		port->parse (now);
	}

	return true;
}

void
FaderPort8::start_midi_handling ()
{
	_input_port->parser()->sysex.connect_same_thread (midi_connections, boost::bind (&FaderPort8::sysex_handler, this, _1, _2, _3));
	_input_port->parser()->poly_pressure.connect_same_thread (midi_connections, boost::bind (&FaderPort8::polypressure_handler, this, _1, _2));
	for (uint8_t i = 0; i < 16; ++i) {
	_input_port->parser()->channel_pitchbend[i].connect_same_thread (midi_connections, boost::bind (&FaderPort8::pitchbend_handler, this, _1, i, _2));
	}
	_input_port->parser()->controller.connect_same_thread (midi_connections, boost::bind (&FaderPort8::controller_handler, this, _1, _2));
	_input_port->parser()->note_on.connect_same_thread (midi_connections, boost::bind (&FaderPort8::note_on_handler, this, _1, _2));
	_input_port->parser()->note_off.connect_same_thread (midi_connections, boost::bind (&FaderPort8::note_off_handler, this, _1, _2));

	/* This connection means that whenever data is ready from the input
	 * port, the relevant thread will invoke our ::midi_input_handler()
	 * method, which will read the data, and invoke the parser.
	 */
	_input_port->xthread().set_receive_handler (sigc::bind (sigc::mem_fun (this, &FaderPort8::midi_input_handler), boost::weak_ptr<AsyncMIDIPort> (_input_port)));
	_input_port->xthread().attach (main_loop()->get_context());
}

size_t
FaderPort8::tx_midi (std::vector<uint8_t> const& d) const
{
	return _output_port->write (&d[0], d.size(), 0);
}

/* ****************************************************************************
 * MIDI Callbacks
 */
void
FaderPort8::polypressure_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	debug_2byte_msg ("PP", tb->controller_number, tb->value);
	// outgoing only (meter)
}

void
FaderPort8::pitchbend_handler (MIDI::Parser &, uint8_t chan, MIDI::pitchbend_t pb)
{
	debug_2byte_msg ("PB", chan, pb);
	/* fader 0..16368 (0x3ff0 -- 1024 steps) */
	_ctrls.midi_fader (chan, pb);
}

void
FaderPort8::controller_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	debug_2byte_msg ("CC", tb->controller_number, tb->value);
	// encoder
	//
	// param 0x10 -> pan/param
	// param 0x3c -> navigator
	//
	// val Bit 7 = direction, Bits 0-6 = number of steps
	if (tb->controller_number == 0x3c) {
		encoder_navigate (tb->value & 0x40 ? true : false, tb->value & 0x3f);
	}
}

void
FaderPort8::note_on_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	debug_2byte_msg ("ON", tb->note_number, tb->velocity);

	/* fader touch */
	if (tb->note_number >= 0x68 && tb->note_number <= 0x6f) {
		_ctrls.midi_touch (tb->note_number - 0x68, tb->velocity);
		return;
	}

	/* special case shift */
	if (tb->note_number == 0x06 || tb->note_number == 0x46) {
		_shift_connection.disconnect ();
		if (_shift_lock) {
			_shift_lock = false;
			ShiftButtonChange (false);
			tx_midi3 (0x90, 0x06, 0x00);
			tx_midi3 (0x90, 0x46, 0x00);
			return;
		}

		Glib::RefPtr<Glib::TimeoutSource> shift_timer =
			Glib::TimeoutSource::create (1000);
		shift_timer->attach (main_loop()->get_context());
		_shift_connection = shift_timer->connect (sigc::mem_fun (*this, &FaderPort8::shift_timeout));

		ShiftButtonChange (true);
		tx_midi3 (0x90, 0x06, 0x7f);
		tx_midi3 (0x90, 0x46, 0x7f);
		return;
	}

	_ctrls.midi_event (tb->note_number, tb->velocity);
}

void
FaderPort8::note_off_handler (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	debug_2byte_msg ("OF", tb->note_number, tb->velocity);

	if (tb->note_number >= 0x68 && tb->note_number <= 0x6f) {
		// fader touch
		_ctrls.midi_touch (tb->note_number - 0x68, tb->velocity);
		return;
	}

	/* special case shift */
	if (tb->note_number == 0x06 || tb->note_number == 0x46) {
		if (_shift_lock) {
			return;
		}
		ShiftButtonChange (false);
		tx_midi3 (0x90, 0x06, 0x00);
		tx_midi3 (0x90, 0x46, 0x00);
		/* just in case this happens concurrently */
		_shift_connection.disconnect ();
		_shift_lock = false;
		return;
	}

	_ctrls.midi_event (tb->note_number, tb->velocity);
}

void
FaderPort8::sysex_handler (MIDI::Parser &p, MIDI::byte *buf, size_t size)
{
#ifndef NDEBUG
	if (DEBUG_ENABLED(DEBUG::FaderPort8)) {
		DEBUG_STR_DECL(a);
		DEBUG_STR_APPEND(a, string_compose ("RECV sysex siz=%1", size));
		for (size_t i=0; i < size; ++i) {
			DEBUG_STR_APPEND(a,hex);
			DEBUG_STR_APPEND(a,"0x");
			DEBUG_STR_APPEND(a,(int)buf[i]);
			DEBUG_STR_APPEND(a,' ');
		}
		DEBUG_STR_APPEND(a,'\n');
		DEBUG_TRACE (DEBUG::FaderPort8, DEBUG_STR(a).str());
	}
#endif
}

/* ****************************************************************************
 * Persistent State
 */
XMLNode&
FaderPort8::get_state ()
{
	XMLNode& node (ControlProtocol::get_state());

	XMLNode* child;

	child = new XMLNode (X_("Input"));
	child->add_child_nocopy (boost::shared_ptr<ARDOUR::Port>(_input_port)->get_state());
	node.add_child_nocopy (*child);


	child = new XMLNode (X_("Output"));
	child->add_child_nocopy (boost::shared_ptr<ARDOUR::Port>(_output_port)->get_state());
	node.add_child_nocopy (*child);

	return node;
}

int
FaderPort8::set_state (const XMLNode& node, int version)
{
	XMLNodeList nlist;
	XMLNodeConstIterator niter;
	XMLNode const* child;

	if (ControlProtocol::set_state (node, version)) {
		return -1;
	}

	if ((child = node.child (X_("Input"))) != 0) {
		XMLNode* portnode = child->child (Port::state_node_name.c_str());
		if (portnode) {
			boost::shared_ptr<ARDOUR::Port>(_input_port)->set_state (*portnode, version);
		}
	}

	if ((child = node.child (X_("Output"))) != 0) {
		XMLNode* portnode = child->child (Port::state_node_name.c_str());
		if (portnode) {
			boost::shared_ptr<ARDOUR::Port>(_output_port)->set_state (*portnode, version);
		}
	}

	return 0;
}
