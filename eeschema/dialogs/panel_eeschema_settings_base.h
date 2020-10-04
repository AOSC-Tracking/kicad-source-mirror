///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Oct 26 2018)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include "widgets/color_swatch.h"
#include <wx/string.h>
#include <wx/checkbox.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/panel.h>

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// Class PANEL_EESCHEMA_SETTINGS_BASE
///////////////////////////////////////////////////////////////////////////////
class PANEL_EESCHEMA_SETTINGS_BASE : public wxPanel
{
	private:

	protected:
		wxCheckBox* m_checkHVOrientation;
		wxCheckBox* m_mouseDragIsDrag;
		wxCheckBox* m_cbAutoStartWires;
		wxStaticText* m_borderColorLabel;
		COLOR_SWATCH* m_borderColorSwatch;
		wxStaticText* m_backgroundColorLabel;
		COLOR_SWATCH* m_backgroundColorSwatch;
		wxCheckBox* m_cbPinSelectionOpt;
		wxCheckBox* m_checkAutoplaceFields;
		wxCheckBox* m_checkAutoplaceJustify;
		wxCheckBox* m_checkAutoplaceAlign;
		wxStaticText* m_hPitchLabel;
		wxTextCtrl* m_hPitchCtrl;
		wxStaticText* m_hPitchUnits;
		wxStaticText* m_vPitchLabel;
		wxTextCtrl* m_vPitchCtrl;
		wxStaticText* m_vPitchUnits;
		wxStaticText* m_labelIncrementLabel;
		wxSpinCtrl* m_spinLabelRepeatStep;
		wxCheckBox* m_footprintPreview;
		wxCheckBox* m_navigatorStaysOpen;

	public:

		PANEL_EESCHEMA_SETTINGS_BASE( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( -1,-1 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );
		~PANEL_EESCHEMA_SETTINGS_BASE();

};

