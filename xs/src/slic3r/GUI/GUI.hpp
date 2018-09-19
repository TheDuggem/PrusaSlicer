#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

#include <string>
#include <vector>
#include "PrintConfig.hpp"
#include "../../callback.hpp"
#include "GUI_ObjectParts.hpp"

#include <wx/intl.h>
#include <wx/string.h>

class wxApp;
class wxWindow;
class wxFrame;
class wxMenuBar;
class wxNotebook;
class wxPanel;
class wxComboCtrl;
class wxString;
class wxArrayString;
class wxArrayLong;
class wxColour;
class wxBoxSizer;
class wxFlexGridSizer;
class wxButton;
class wxFileDialog;
class wxStaticBitmap;
class wxFont;
class wxTopLevelWindow;

namespace Slic3r { 

class PresetBundle;
class PresetCollection;
class Print;
class ProgressStatusBar;
class AppConfig;
class PresetUpdater;
class DynamicPrintConfig;
class TabIface;
class PreviewIface;
class Print;
class GCodePreviewData;

#define _(s)    Slic3r::GUI::I18N::translate((s))

namespace GUI { namespace I18N {
	inline wxString translate(const char *s)    	 { return wxGetTranslation(wxString(s, wxConvUTF8)); }
	inline wxString translate(const wchar_t *s) 	 { return wxGetTranslation(s); }
	inline wxString translate(const std::string &s)  { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)); }
	inline wxString translate(const std::wstring &s) { return wxGetTranslation(s.c_str()); }
} }

// !!! If you needed to translate some wxString,
// !!! please use _(L(string))
// !!! _() - is a standard wxWidgets macro to translate
// !!! L() is used only for marking localizable string 
// !!! It will be used in "xgettext" to create a Locating Message Catalog.
#define L(s) s

//! macro used to localization, return wxScopedCharBuffer
//! With wxConvUTF8 explicitly specify that the source string is already in UTF-8 encoding
#define _CHB(s) wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str()

// Minimal buffer length for translated string (char buf[MIN_BUF_LENGTH_FOR_L])
#define MIN_BUF_LENGTH_FOR_L	512

namespace GUI {

class Tab;
class ConfigOptionsGroup;
// Map from an file_type name to full file wildcard name.
typedef std::map<std::string, std::string> t_file_wild_card;
inline t_file_wild_card& get_file_wild_card() {
	static t_file_wild_card FILE_WILDCARDS;
	if (FILE_WILDCARDS.empty()){
		FILE_WILDCARDS["known"]	= "Known files (*.stl, *.obj, *.amf, *.xml, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.amf;*.AMF;*.xml;*.XML;*.prusa;*.PRUSA";
		FILE_WILDCARDS["stl"]	= "STL files (*.stl)|*.stl;*.STL";
		FILE_WILDCARDS["obj"]	= "OBJ files (*.obj)|*.obj;*.OBJ";
        FILE_WILDCARDS["amf"]	= "AMF files (*.amf)|*.zip.amf;*.amf;*.AMF;*.xml;*.XML";
        FILE_WILDCARDS["3mf"]	= "3MF files (*.3mf)|*.3mf;*.3MF;";
        FILE_WILDCARDS["prusa"]	= "Prusa Control files (*.prusa)|*.prusa;*.PRUSA";
		FILE_WILDCARDS["ini"]	= "INI files *.ini|*.ini;*.INI";
		FILE_WILDCARDS["gcode"] = "G-code files (*.gcode, *.gco, *.g, *.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC";
		FILE_WILDCARDS["svg"]	= "SVG files *.svg|*.svg;*.SVG";
	}
	return FILE_WILDCARDS;
}

struct PresetTab {
    std::string       name;
    Tab*              panel;
    PrinterTechnology technology;
};


void disable_screensaver();
void enable_screensaver();
bool debugged();
void break_to_debugger();

// Passing the wxWidgets GUI classes instantiated by the Perl part to C++.
void set_wxapp(wxApp *app);
void set_main_frame(wxFrame *main_frame);
void set_progress_status_bar(ProgressStatusBar *prsb);
void set_tab_panel(wxNotebook *tab_panel);
void set_plater(wxPanel *plater);
void set_app_config(AppConfig *app_config);
void set_preset_bundle(PresetBundle *preset_bundle);
void set_preset_updater(PresetUpdater *updater);
void set_objects_from_perl(	wxWindow* parent,
							wxBoxSizer *frequently_changed_parameters_sizer,
							wxBoxSizer *info_sizer,
							wxButton *btn_export_gcode,
							wxButton *btn_reslice,
							wxButton *btn_print,
							wxButton *btn_send_gcode,
							wxStaticBitmap *manifold_warning_icon);
void set_show_print_info(bool show);
void set_show_manifold_warning_icon(bool show);
void set_objects_list_sizer(wxBoxSizer *objects_list_sizer);

AppConfig*		get_app_config();
wxApp*			get_app();
PresetBundle*	get_preset_bundle();
wxFrame*		get_main_frame();
ProgressStatusBar* get_progress_status_bar();
wxNotebook *	get_tab_panel();
wxNotebook*		get_tab_panel();

const wxColour& get_label_clr_modified();
const wxColour& get_label_clr_sys();
const wxColour& get_label_clr_default();
unsigned get_colour_approx_luma(const wxColour &colour);
void set_label_clr_modified(const wxColour& clr);
void set_label_clr_sys(const wxColour& clr);

const wxFont& small_font();
const wxFont& bold_font();

void open_model(wxWindow *parent, wxArrayString& input_files);

wxWindow*			get_right_panel();
const size_t&		label_width();

Tab*         get_tab(const std::string& name);
const std::vector<PresetTab>& get_preset_tabs();

extern void add_menus(wxMenuBar *menu, int event_preferences_changed, int event_language_change);

// This is called when closing the application, when loading a config file or when starting the config wizard
// to notify the user whether he is aware that some preset changes will be lost.
extern bool check_unsaved_changes();

// Checks if configuration wizard needs to run, calls config_wizard if so.
// Returns whether the Wizard ran.
extern bool config_wizard_startup(bool app_config_exists);

// Opens the configuration wizard, returns true if wizard is finished & accepted.
// The run_reason argument is actually ConfigWizard::RunReason, but int is used here because of Perl.
extern void config_wizard(int run_reason);

// Create "Preferences" dialog after selecting menu "Preferences" in Perl part
extern void open_preferences_dialog(int event_preferences);

// Create a new preset tab (print, filament and printer),
void create_preset_tabs(int event_value_change, int event_presets_changed);
TabIface* get_preset_tab_iface(char *name);

PreviewIface* create_preview_iface(wxNotebook* notebook, DynamicPrintConfig* config, Print* print, GCodePreviewData* gcode_preview_data);

// add it at the end of the tab panel.
void add_created_tab(Tab* panel, int event_value_change, int event_presets_changed);
// Change option value in config
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

// Update UI / Tabs to reflect changes in the currently loaded presets
void load_current_presets();

void show_error(wxWindow* parent, const wxString& message);
void show_error_id(int id, const std::string& message);   // For Perl
void show_info(wxWindow* parent, const wxString& message, const wxString& title);
void warning_catcher(wxWindow* parent, const wxString& message);

// Assign a Lambda to the print object to emit a wxWidgets Command with the provided ID
// to deliver a progress status message.
void set_print_callback_event(Print *print, int id);

// load language saved at application config 
bool load_language();
// save language at application config 
void save_language();
// get list of installed languages 
void get_installed_languages(wxArrayString & names, wxArrayLong & identifiers);
// select language from the list of installed languages
bool select_language(wxArrayString & names, wxArrayLong & identifiers);
// update right panel of the Plater according to view mode
void update_mode();

void show_info_sizer(const bool show);

std::vector<Tab *>& get_tabs_list();
bool checked_tab(Tab* tab);
void delete_tab_from_list(Tab* tab);

// Creates a wxCheckListBoxComboPopup inside the given wxComboCtrl, filled with the given text and items.
// Items are all initialized to the given value.
// Items must be separated by '|', for example "Item1|Item2|Item3", and so on.
void create_combochecklist(wxComboCtrl* comboCtrl, std::string text, std::string items, bool initial_value);

// Returns the current state of the items listed in the wxCheckListBoxComboPopup contained in the given wxComboCtrl,
// encoded inside an int.
int combochecklist_get_flags(wxComboCtrl* comboCtrl);

// Return translated std::string as a wxString
wxString	L_str(const std::string &str);
// Return wxString from std::string in UTF8
wxString	from_u8(const std::string &str);
// Return std::string in UTF8 from wxString
std::string	into_u8(const wxString &str);

void set_model_events_from_perl(Model &model,
							    int event_object_selection_changed,
							    int event_object_settings_changed,
							    int event_remove_object, 
							    int event_update_scene);
void add_frequently_changed_parameters(wxWindow* parent, wxBoxSizer* sizer, wxFlexGridSizer* preset_sizer);
// Update view mode according to selected menu 
void update_mode();
bool is_expert_mode();

// Callback to trigger a configuration update timer on the Plater.
static PerlCallback g_on_request_update_callback;
 
ConfigOptionsGroup* get_optgroup(size_t i); 
std::vector <std::shared_ptr<ConfigOptionsGroup>>& get_optgroups();
wxButton*			get_wiping_dialog_button();

void add_export_option(wxFileDialog* dlg, const std::string& format);
int get_export_option(wxFileDialog* dlg);

// Returns the dimensions of the screen on which the main frame is displayed
bool get_current_screen_size(wxWindow *window, unsigned &width, unsigned &height);

// Save window size and maximized status into AppConfig
void save_window_size(wxTopLevelWindow *window, const std::string &name);
// Restore the above
void restore_window_size(wxTopLevelWindow *window, const std::string &name);

// Update buttons view according to enable/disable
void enable_action_buttons(bool enable);

// Display an About dialog
extern void about();
// Ask the destop to open the datadir using the default file explorer.
extern void desktop_open_datadir_folder();

} // namespace GUI
} // namespace Slic3r

#endif
