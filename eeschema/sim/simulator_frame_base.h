///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 3.10.1-0-g8feb16b)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
class ACTION_TOOLBAR;
class WX_GRID;

#include "sim_notebook.h"
#include "kiway_player.h"
#include <wx/gdicmn.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/aui/auibook.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/splitter.h>
#include <wx/srchctrl.h>
#include <wx/grid.h>
#include <wx/frame.h>

///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
/// Class SIMULATOR_FRAME_BASE
///////////////////////////////////////////////////////////////////////////////
class SIMULATOR_FRAME_BASE : public KIWAY_PLAYER
{
	private:

	protected:
		wxBoxSizer* m_sizerMain;
		ACTION_TOOLBAR* m_toolBar;
		wxSplitterWindow* m_splitterLeftRight;
		wxPanel* m_panelLeft;
		wxBoxSizer* m_sizer11;
		wxSplitterWindow* m_splitterPlotAndConsole;
		wxPanel* m_plotPanel;
		wxBoxSizer* m_sizerPlot;
		SIM_NOTEBOOK* m_plotNotebook;
		wxPanel* m_panelConsole;
		wxBoxSizer* m_sizerConsole;
		wxTextCtrl* m_simConsole;
		wxPanel* m_sidePanel;
		wxBoxSizer* m_sideSizer;
		wxSplitterWindow* m_splitterSignals;
		wxPanel* m_panelSignals;
		wxSearchCtrl* m_filter;
		WX_GRID* m_signalsGrid;
		wxPanel* m_panelCMT;
		wxSplitterWindow* m_splitterCursors;
		wxPanel* m_panelCursors;
		WX_GRID* m_cursorsGrid;
		wxPanel* m_panelMT;
		wxSplitterWindow* m_splitterMeasurements;
		wxPanel* m_panelMeasurements;
		WX_GRID* m_measurementsGrid;
		wxPanel* m_panelTuners;
		wxBoxSizer* m_sizerTuners;

		// Virtual event handlers, override them in your derived class
		virtual void onPlotDragged( wxAuiNotebookEvent& event ) { event.Skip(); }
		virtual void onPlotChanged( wxAuiNotebookEvent& event ) { event.Skip(); }
		virtual void onPlotClose( wxAuiNotebookEvent& event ) { event.Skip(); }
		virtual void onPlotClosed( wxAuiNotebookEvent& event ) { event.Skip(); }
		virtual void OnFilterMouseMoved( wxMouseEvent& event ) { event.Skip(); }
		virtual void OnFilterText( wxCommandEvent& event ) { event.Skip(); }
		virtual void onSignalsGridCellChanged( wxGridEvent& event ) { event.Skip(); }
		virtual void onCursorsGridCellChanged( wxGridEvent& event ) { event.Skip(); }
		virtual void onMeasurementsGridCellChanged( wxGridEvent& event ) { event.Skip(); }


	public:

		SIMULATOR_FRAME_BASE( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Spice Simulator"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( -1,-1 ), long style = wxDEFAULT_FRAME_STYLE|wxTAB_TRAVERSAL, const wxString& name = wxT("SIM_PLOT_FRAME") );

		~SIMULATOR_FRAME_BASE();

		void m_splitterLeftRightOnIdle( wxIdleEvent& )
		{
			m_splitterLeftRight->SetSashPosition( 700 );
			m_splitterLeftRight->Disconnect( wxEVT_IDLE, wxIdleEventHandler( SIMULATOR_FRAME_BASE::m_splitterLeftRightOnIdle ), NULL, this );
		}

		void m_splitterPlotAndConsoleOnIdle( wxIdleEvent& )
		{
			m_splitterPlotAndConsole->SetSashPosition( 500 );
			m_splitterPlotAndConsole->Disconnect( wxEVT_IDLE, wxIdleEventHandler( SIMULATOR_FRAME_BASE::m_splitterPlotAndConsoleOnIdle ), NULL, this );
		}

		void m_splitterSignalsOnIdle( wxIdleEvent& )
		{
			m_splitterSignals->SetSashPosition( 0 );
			m_splitterSignals->Disconnect( wxEVT_IDLE, wxIdleEventHandler( SIMULATOR_FRAME_BASE::m_splitterSignalsOnIdle ), NULL, this );
		}

		void m_splitterCursorsOnIdle( wxIdleEvent& )
		{
			m_splitterCursors->SetSashPosition( 0 );
			m_splitterCursors->Disconnect( wxEVT_IDLE, wxIdleEventHandler( SIMULATOR_FRAME_BASE::m_splitterCursorsOnIdle ), NULL, this );
		}

		void m_splitterMeasurementsOnIdle( wxIdleEvent& )
		{
			m_splitterMeasurements->SetSashPosition( 0 );
			m_splitterMeasurements->Disconnect( wxEVT_IDLE, wxIdleEventHandler( SIMULATOR_FRAME_BASE::m_splitterMeasurementsOnIdle ), NULL, this );
		}

};

