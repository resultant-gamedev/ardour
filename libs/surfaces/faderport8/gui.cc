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

#include <gtkmm/alignment.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>

#include "pbd/unwind.h"
#include "pbd/strsplit.h"
#include "pbd/file_utils.h"

#include "gtkmm2ext/bindings.h"
#include "gtkmm2ext/gtk_ui.h"
#include "gtkmm2ext/gui_thread.h"
#include "gtkmm2ext/utils.h"

#include "ardour/audioengine.h"
#include "ardour/filesystem_paths.h"

#include "faderport8.h"
#include "gui.h"

#include "pbd/i18n.h"

using namespace PBD;
using namespace ARDOUR;
using namespace ArdourSurface;
using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;

void*
FaderPort8::get_gui () const
{
	if (!gui) {
		const_cast<FaderPort8*>(this)->build_gui ();
	}
	static_cast<Gtk::VBox*>(gui)->show_all();
	return gui;
}

void
FaderPort8::tear_down_gui ()
{
	if (gui) {
		Gtk::Widget *w = static_cast<Gtk::VBox*>(gui)->get_parent();
		if (w) {
			w->hide();
			delete w;
		}
	}
	delete static_cast<FP8GUI*> (gui);
	gui = 0;
}

void
FaderPort8::build_gui ()
{
	gui = (void*) new FP8GUI (*this);
}

/* ****************************************************************************/

FP8GUI::FP8GUI (FaderPort8& p)
	: fp (p)
	, table (2, 5)
	, ignore_active_change (false)
{
	set_border_width (12);

	table.set_row_spacings (4);
	table.set_col_spacings (6);
	table.set_border_width (12);
	table.set_homogeneous (false);

	Gtk::Label* l;
	int row = 0;

	input_combo.pack_start (midi_port_columns.short_name);
	output_combo.pack_start (midi_port_columns.short_name);

	input_combo.signal_changed().connect (sigc::bind (sigc::mem_fun (*this, &FP8GUI::active_port_changed), &input_combo, true));
	output_combo.signal_changed().connect (sigc::bind (sigc::mem_fun (*this, &FP8GUI::active_port_changed), &output_combo, false));

	l = manage (new Gtk::Label);
	l->set_markup (string_compose ("<span weight=\"bold\">%1</span>", _("Incoming MIDI on:")));
	l->set_alignment (1.0, 0.5);
	table.attach (*l, 0, 1, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0));
	table.attach (input_combo, 1, 2, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0), 0, 0);
	row++;

	l = manage (new Gtk::Label);
	l->set_markup (string_compose ("<span weight=\"bold\">%1</span>", _("Outgoing MIDI on:")));
	l->set_alignment (1.0, 0.5);
	table.attach (*l, 0, 1, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0));
	table.attach (output_combo, 1, 2, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0), 0, 0);
	row++;

	pack_start (table);

	/* update the port connection combos */
	update_port_combos ();
	/* catch future changes to connection state */
	fp.ConnectionChange.connect (connection_change_connection, invalidator (*this), boost::bind (&FP8GUI::connection_handler, this), gui_context());
}

FP8GUI::~FP8GUI ()
{
}

void
FP8GUI::connection_handler ()
{
	PBD::Unwinder<bool> ici (ignore_active_change, true);
	update_port_combos ();
}

void
FP8GUI::update_port_combos ()
{
	vector<string> midi_inputs;
	vector<string> midi_outputs;

	ARDOUR::AudioEngine::instance()->get_ports ("", ARDOUR::DataType::MIDI, ARDOUR::PortFlags (ARDOUR::IsOutput|ARDOUR::IsTerminal), midi_inputs);
	ARDOUR::AudioEngine::instance()->get_ports ("", ARDOUR::DataType::MIDI, ARDOUR::PortFlags (ARDOUR::IsInput|ARDOUR::IsTerminal), midi_outputs);

	Glib::RefPtr<Gtk::ListStore> input = build_midi_port_list (midi_inputs, true);
	Glib::RefPtr<Gtk::ListStore> output = build_midi_port_list (midi_outputs, false);
	bool input_found = false;
	bool output_found = false;
	int n;

	input_combo.set_model (input);
	output_combo.set_model (output);

	Gtk::TreeModel::Children children = input->children();
	Gtk::TreeModel::Children::iterator i;
	i = children.begin();
	++i; /* skip "Disconnected" */

	for (n = 1;  i != children.end(); ++i, ++n) {
		string port_name = (*i)[midi_port_columns.full_name];
		if (fp.input_port()->connected_to (port_name)) {
			input_combo.set_active (n);
			input_found = true;
			break;
		}
	}

	if (!input_found) {
		input_combo.set_active (0); /* disconnected */
	}

	children = output->children();
	i = children.begin();
	++i; /* skip "Disconnected" */

	for (n = 1;  i != children.end(); ++i, ++n) {
		string port_name = (*i)[midi_port_columns.full_name];
		if (fp.output_port()->connected_to (port_name)) {
			output_combo.set_active (n);
			output_found = true;
			break;
		}
	}

	if (!output_found) {
		output_combo.set_active (0); /* disconnected */
	}
}


Glib::RefPtr<Gtk::ListStore>
FP8GUI::build_midi_port_list (vector<string> const & ports, bool for_input)
{
	Glib::RefPtr<Gtk::ListStore> store = ListStore::create (midi_port_columns);
	TreeModel::Row row;

	row = *store->append ();
	row[midi_port_columns.full_name] = string();
	row[midi_port_columns.short_name] = _("Disconnected");

	for (vector<string>::const_iterator p = ports.begin(); p != ports.end(); ++p) {
		row = *store->append ();
		row[midi_port_columns.full_name] = *p;
		std::string pn = ARDOUR::AudioEngine::instance()->get_pretty_name_by_name (*p);
		if (pn.empty ()) {
			pn = (*p).substr ((*p).find (':') + 1);
		}
		row[midi_port_columns.short_name] = pn;
	}

	return store;
}

void
FP8GUI::active_port_changed (Gtk::ComboBox* combo, bool for_input)
{
	if (ignore_active_change) {
		return;
	}

	TreeModel::iterator active = combo->get_active ();
	string new_port = (*active)[midi_port_columns.full_name];

	if (new_port.empty()) {
		if (for_input) {
			fp.input_port()->disconnect_all ();
		} else {
			fp.output_port()->disconnect_all ();
		}

		return;
	}

	if (for_input) {
		if (!fp.input_port()->connected_to (new_port)) {
			fp.input_port()->disconnect_all ();
			fp.input_port()->connect (new_port);
		}
	} else {
		if (!fp.output_port()->connected_to (new_port)) {
			fp.output_port()->disconnect_all ();
			fp.output_port()->connect (new_port);
		}
	}
}
