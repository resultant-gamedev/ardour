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

#ifndef __ardour_faderport8_gui_h__
#define __ardour_faderport8_gui_h__

#include <vector>
#include <string>

#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/image.h>
#include <gtkmm/table.h>
#include <gtkmm/treestore.h>

namespace Gtk {
	class CellRendererCombo;
	class ListStore;
}

#include "faderport8.h"

namespace ArdourSurface {

class FP8GUI : public Gtk::VBox
{
public:
	FP8GUI (FaderPort8&);
	~FP8GUI ();

private:
	FaderPort8& fp;
	Gtk::Table table;
	Gtk::Table action_table;
	Gtk::ComboBox input_combo;
	Gtk::ComboBox output_combo;
	Gtk::Image    image;

	void update_port_combos ();
	PBD::ScopedConnection connection_change_connection;
	void connection_handler ();

	struct MidiPortColumns : public Gtk::TreeModel::ColumnRecord {
		MidiPortColumns() {
			add (short_name);
			add (full_name);
		}
		Gtk::TreeModelColumn<std::string> short_name;
		Gtk::TreeModelColumn<std::string> full_name;
	};

	MidiPortColumns midi_port_columns;
	bool ignore_active_change;

	Glib::RefPtr<Gtk::ListStore> build_midi_port_list (std::vector<std::string> const & ports, bool for_input);
	void active_port_changed (Gtk::ComboBox*,bool for_input);


	bool find_action_in_model (const Gtk::TreeModel::iterator& iter, std::string const & action_path, Gtk::TreeModel::iterator* found);

};

}

#endif /* __ardour_faderport8_gui_h__ */
