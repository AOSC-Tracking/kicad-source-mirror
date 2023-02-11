/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016-2023 CERN
 * Copyright (C) 2016-2023 KiCad Developers, see AUTHORS.txt for contributors.
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * https://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <wx/debug.h>

// For some obscure reason, needed on msys2 with some wxWidgets versions (3.0) to avoid
// undefined symbol at link stage (due to use of #include <pegtl.hpp>)
// Should not create issues on other platforms
#include <wx/menu.h>

#include <project/project_file.h>
#include <sch_edit_frame.h>
#include <kiway.h>
#include <confirm.h>
#include <bitmaps.h>
#include <wildcards_and_files_ext.h>
#include <widgets/tuner_slider.h>
#include <tool/tool_manager.h>
#include <tool/tool_dispatcher.h>
#include <tool/action_manager.h>
#include <tool/action_toolbar.h>
#include <tool/common_control.h>
#include <tools/simulator_control.h>
#include <tools/ee_actions.h>
#include <string_utils.h>
#include <pgm_base.h>
#include "ngspice.h"
#include "sim_plot_frame.h"
#include "sim_plot_panel.h"
#include "spice_simulator.h"
#include "spice_reporter.h"
#include <menus_helpers.h>
#include <eeschema_settings.h>

#include <memory>


SIM_PLOT_TYPE operator|( SIM_PLOT_TYPE aFirst, SIM_PLOT_TYPE aSecond )
{
    int res = (int) aFirst | (int) aSecond;

    return (SIM_PLOT_TYPE) res;
}


class SIM_THREAD_REPORTER : public SPICE_REPORTER
{
public:
    SIM_THREAD_REPORTER( SIM_PLOT_FRAME* aParent ) :
        m_parent( aParent )
    {
    }

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        wxCommandEvent* event = new wxCommandEvent( EVT_SIM_REPORT );
        event->SetString( aText );
        wxQueueEvent( m_parent, event );
        return *this;
    }

    bool HasMessage() const override
    {
        return false;       // Technically "indeterminate" rather than false.
    }

    void OnSimStateChange( SPICE_SIMULATOR* aObject, SIM_STATE aNewState ) override
    {
        wxCommandEvent* event = nullptr;

        switch( aNewState )
        {
        case SIM_IDLE:    event = new wxCommandEvent( EVT_SIM_FINISHED ); break;
        case SIM_RUNNING: event = new wxCommandEvent( EVT_SIM_STARTED );  break;
        default:          wxFAIL;                                         return;
        }

        wxQueueEvent( m_parent, event );
    }

private:
    SIM_PLOT_FRAME* m_parent;
};


SIM_PLOT_FRAME::SIM_PLOT_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
        SIM_PLOT_FRAME_BASE( aParent ),
        m_lastSimPlot( nullptr ),
        m_darkMode( true ),
        m_plotNumber( 0 ),
        m_simFinished( false )
{
    SetKiway( this, aKiway );
    m_signalsIconColorList = nullptr;

    m_schematicFrame = (SCH_EDIT_FRAME*) Kiway().Player( FRAME_SCH, false );
    wxASSERT( m_schematicFrame );

    // Give an icon
    wxIcon icon;
    icon.CopyFromBitmap( KiBitmap( BITMAPS::simulator ) );
    SetIcon( icon );

    m_simulator = SIMULATOR::CreateInstance( "ngspice" );
    wxASSERT( m_simulator );

    // Get the previous size and position of windows:
    LoadSettings( config() );

    // Prepare the color list to plot traces
    SIM_PLOT_COLORS::FillDefaultColorList( m_darkMode );

    m_simulator->Init();

    m_reporter = new SIM_THREAD_REPORTER( this );
    m_simulator->SetReporter( m_reporter );

    m_circuitModel = std::make_shared<NGSPICE_CIRCUIT_MODEL>( &m_schematicFrame->Schematic(), this );

    setupTools();
    setupUIConditions();

    ReCreateHToolbar();
    ReCreateMenuBar();

    Bind( wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( SIM_PLOT_FRAME::onExit ), this,
          wxID_EXIT );

    Bind( EVT_SIM_UPDATE, &SIM_PLOT_FRAME::onSimUpdate, this );
    Bind( EVT_SIM_REPORT, &SIM_PLOT_FRAME::onSimReport, this );
    Bind( EVT_SIM_STARTED, &SIM_PLOT_FRAME::onSimStarted, this );
    Bind( EVT_SIM_FINISHED, &SIM_PLOT_FRAME::onSimFinished, this );
    Bind( EVT_SIM_CURSOR_UPDATE, &SIM_PLOT_FRAME::onCursorUpdate, this );

    Bind( EVT_WORKBOOK_MODIFIED, &SIM_PLOT_FRAME::onWorkbookModified, this );
    Bind( EVT_WORKBOOK_CLR_MODIFIED, &SIM_PLOT_FRAME::onWorkbookClrModified, this );

#ifndef wxHAS_NATIVE_TABART
    // Default non-native tab art has ugly gradients we don't want
    m_workbook->SetArtProvider( new wxAuiSimpleTabArt() );
#endif

    // Ensure new items are taken in account by sizers:
    Layout();

    // resize the subwindows size. At least on Windows, calling wxSafeYield before
    // resizing the subwindows forces the wxSplitWindows size events automatically generated
    // by wxWidgets to be executed before our resize code.
    // Otherwise, the changes made by setSubWindowsSashSize are overwritten by one these
    // events
    wxSafeYield();
    setSubWindowsSashSize();

    // Ensure the window is on top
    Raise();

    initWorkbook();
    updateTitle();
}


SIM_PLOT_FRAME::~SIM_PLOT_FRAME()
{
    NULL_REPORTER devnull;

    m_simulator->Attach( nullptr, devnull );
    m_simulator->SetReporter( nullptr );
    delete m_reporter;
    delete m_signalsIconColorList;
}


void SIM_PLOT_FRAME::setupTools()
{
    // Create the manager
    m_toolManager = new TOOL_MANAGER;
    m_toolManager->SetEnvironment( nullptr, nullptr, nullptr, config(), this );

    m_toolDispatcher = new TOOL_DISPATCHER( m_toolManager );

    // Attach the events to the tool dispatcher
    Bind( wxEVT_CHAR, &TOOL_DISPATCHER::DispatchWxEvent, m_toolDispatcher );
    Bind( wxEVT_CHAR_HOOK, &TOOL_DISPATCHER::DispatchWxEvent, m_toolDispatcher );

    // Register tools
    m_toolManager->RegisterTool( new COMMON_CONTROL );
    m_toolManager->RegisterTool( new SIMULATOR_CONTROL );
    m_toolManager->InitTools();
}


void SIM_PLOT_FRAME::LoadSettings( APP_SETTINGS_BASE* aCfg )
{
    EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( aCfg );
    wxASSERT( cfg );

    if( cfg )
    {
        EDA_BASE_FRAME::LoadSettings( cfg );

        // Read subwindows sizes (should be > 0 )
        m_splitterLeftRightSashPosition      = cfg->m_Simulator.plot_panel_width;
        m_splitterPlotAndConsoleSashPosition = cfg->m_Simulator.plot_panel_height;
        m_splitterSignalsSashPosition        = cfg->m_Simulator.signal_panel_height;
        m_splitterTuneValuesSashPosition     = cfg->m_Simulator.cursors_panel_height;
        m_darkMode                           = !cfg->m_Simulator.white_background;
    }

    PROJECT_FILE& project = Prj().GetProjectFile();

    NGSPICE* currentSim = dynamic_cast<NGSPICE*>( m_simulator.get() );

    if( currentSim )
        m_simulator->Settings() = project.m_SchematicSettings->m_NgspiceSimulatorSettings;
}


void SIM_PLOT_FRAME::SaveSettings( APP_SETTINGS_BASE* aCfg )
{
    EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( aCfg );
    wxASSERT( cfg );

    if( cfg )
    {
        EDA_BASE_FRAME::SaveSettings( cfg );

        cfg->m_Simulator.plot_panel_width     = m_splitterLeftRight->GetSashPosition();
        cfg->m_Simulator.plot_panel_height    = m_splitterPlotAndConsole->GetSashPosition();
        cfg->m_Simulator.signal_panel_height  = m_splitterSignals->GetSashPosition();
        cfg->m_Simulator.cursors_panel_height = m_splitterTuneValues->GetSashPosition();
        cfg->m_Simulator.white_background     = !m_darkMode;
    }

    if( !m_isNonUserClose )     // If we're exiting the project has already been released.
    {
        PROJECT_FILE& project = Prj().GetProjectFile();

        if( project.m_SchematicSettings )
            project.m_SchematicSettings->m_NgspiceSimulatorSettings->SaveToFile();

        if( m_schematicFrame )
            m_schematicFrame->SaveProjectSettings();
    }
}


WINDOW_SETTINGS* SIM_PLOT_FRAME::GetWindowSettings( APP_SETTINGS_BASE* aCfg )
{
    EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( aCfg );
    wxASSERT( cfg );

    return cfg ? &cfg->m_Simulator.window : nullptr;
}


void SIM_PLOT_FRAME::initWorkbook()
{
    // Removed for the time being. We cannot run the simulation on simulator launch, as it may
    // take a lot of time, confusing the user.
    // TODO: Change workbook loading routines so that they don't run the simulation until the user
    // initiates it.

    /*if( !m_simulator->Settings()->GetWorkbookFilename().IsEmpty() )
    {
        wxFileName filename = m_simulator->Settings()->GetWorkbookFilename();
        filename.SetPath( Prj().GetProjectPath() );

        if( !loadWorkbook( filename.GetFullPath() ) )
            m_simulator->Settings()->SetWorkbookFilename( "" );
    }*/
}


void SIM_PLOT_FRAME::updateTitle()
{
    bool     unsaved = true;
    bool     readOnly = false;
    wxString title;

    if( m_simulator && m_simulator->Settings() )
    {
        wxFileName filename = Prj().AbsolutePath( m_simulator->Settings()->GetWorkbookFilename() );

        if( filename.IsOk() && filename.FileExists() )
        {
            unsaved = false;
            readOnly = !filename.IsFileWritable();
        }

        if( m_workbook->IsModified() )
            title = wxT( "*" ) + filename.GetName();
        else
            title = filename.GetName();
    }

    if( readOnly )
        title += wxS( " " ) + _( "[Read Only]" );

    if( unsaved )
        title += wxS( " " ) + _( "[Unsaved]" );

    title += wxT( " \u2014 " ) + _( "Spice Simulator" );

    SetTitle( title );
}


void SIM_PLOT_FRAME::setSubWindowsSashSize()
{
    if( m_splitterLeftRightSashPosition > 0 )
        m_splitterLeftRight->SetSashPosition( m_splitterLeftRightSashPosition );

    if( m_splitterPlotAndConsoleSashPosition > 0 )
        m_splitterPlotAndConsole->SetSashPosition( m_splitterPlotAndConsoleSashPosition );

    if( m_splitterSignalsSashPosition > 0 )
        m_splitterSignals->SetSashPosition( m_splitterSignalsSashPosition );

    if( m_splitterTuneValuesSashPosition > 0 )
        m_splitterTuneValues->SetSashPosition( m_splitterTuneValuesSashPosition );
}


void SIM_PLOT_FRAME::StartSimulation( const wxString& aSimCommand )
{
    wxCHECK_RET( m_circuitModel->CommandToSimType( GetCurrentSimCommand() ) != ST_UNKNOWN,
                 wxT( "Unknown simulation type" ) );

    m_simConsole->Clear();

    if( aSimCommand != wxEmptyString )
        m_circuitModel->SetSimCommandOverride( aSimCommand );

    m_circuitModel->SetSimOptions( GetCurrentOptions() );

    wxString           errors;
    WX_STRING_REPORTER reporter( &errors );

    if( !m_schematicFrame->ReadyToNetlist( _( "Simulator requires a fully annotated schematic." ) )
            || !m_simulator->Attach( m_circuitModel, reporter ) )
    {
        DisplayErrorMessage( this, _( "Errors during netlist generation; simulation aborted.\n\n" )
                                   + errors );
        return;
    }

    SIM_PANEL_BASE* plotWindow = getCurrentPlotWindow();
    wxString        sheetSimCommand = m_circuitModel->GetSheetSimCommand();

    if( plotWindow
            && plotWindow->GetType() == NGSPICE_CIRCUIT_MODEL::CommandToSimType( sheetSimCommand ) )
    {
        if( m_circuitModel->GetSimCommandOverride().IsEmpty() )
        {
            m_workbook->SetSimCommand( plotWindow, sheetSimCommand );
        }
        else if( sheetSimCommand != m_circuitModel->GetLastSheetSimCommand() )
        {
            if( IsOK( this, _( "Schematic sheet simulation command directive has changed.  Do you "
                               "wish to update the Simulation Command?" ) ) )
            {
                m_circuitModel->SetSimCommandOverride( wxEmptyString );
                m_workbook->SetSimCommand( plotWindow, sheetSimCommand );
            }
        }
    }

    std::unique_lock<std::mutex> simulatorLock( m_simulator->GetMutex(), std::try_to_lock );

    if( simulatorLock.owns_lock() )
    {
        wxBusyCursor toggle;

        applyTuners();

        // Prevents memory leak on succeding simulations by deleting old vectors
        m_simulator->Clean();
        m_simulator->Run();
    }
    else
    {
        DisplayErrorMessage( this, _( "Another simulation is already running." ) );
    }
}


SIM_PANEL_BASE* SIM_PLOT_FRAME::NewPlotPanel( wxString aSimCommand, int aOptions )
{
    SIM_PANEL_BASE* plotPanel = nullptr;
    SIM_TYPE        simType   = NGSPICE_CIRCUIT_MODEL::CommandToSimType( aSimCommand );

    if( SIM_PANEL_BASE::IsPlottable( simType ) )
    {
        SIM_PLOT_PANEL* panel;
        panel = new SIM_PLOT_PANEL( aSimCommand, aOptions, m_workbook, wxID_ANY );

        panel->GetPlotWin()->EnableMouseWheelPan(
                Pgm().GetCommonSettings()->m_Input.scroll_modifier_zoom != 0 );

        plotPanel = dynamic_cast<SIM_PANEL_BASE*>( panel );
    }
    else
    {
        SIM_NOPLOT_PANEL* panel;
        panel = new SIM_NOPLOT_PANEL( aSimCommand, aOptions, m_workbook, wxID_ANY );
        plotPanel = dynamic_cast<SIM_PANEL_BASE*>( panel );
    }

    wxString pageTitle( m_simulator->TypeToName( simType, true ) );
    pageTitle.Prepend( wxString::Format( _( "Plot%u - " ), (unsigned int) ++m_plotNumber ) );

    m_workbook->AddPage( dynamic_cast<wxWindow*>( plotPanel ), pageTitle, true );

    return plotPanel;
}


void SIM_PLOT_FRAME::AddVoltagePlot( const wxString& aNetName )
{
    addPlot( aNetName, SPT_VOLTAGE );
}


void SIM_PLOT_FRAME::AddCurrentPlot( const wxString& aDeviceName )
{
    addPlot( aDeviceName, SPT_CURRENT );
}


void SIM_PLOT_FRAME::AddTuner( const SCH_SHEET_PATH& aSheetPath, SCH_SYMBOL* aSymbol )
{
    SIM_PANEL_BASE* plotPanel = getCurrentPlotWindow();

    if( !plotPanel )
        return;

    wxString ref = aSymbol->GetRef( &aSheetPath );

    // Do not add multiple instances for the same component.
    for( TUNER_SLIDER* tuner : m_tuners )
    {
        if( tuner->GetSymbolRef() == ref )
            return;
    }

    const SPICE_ITEM* item = GetExporter()->FindItem( std::string( ref.ToUTF8() ) );

    // Do nothing if the symbol is not tunable.
    if( !item || !item->model->GetTunerParam() )
        return;

    try
    {
        TUNER_SLIDER* tuner = new TUNER_SLIDER( this, m_tunePanel, aSheetPath, aSymbol );
        m_tuneSizer->Add( tuner );
        m_tuners.push_back( tuner );
        m_tunePanel->Layout();
    }
    catch( const KI_PARAM_ERROR& e )
    {
        DisplayErrorMessage( nullptr, e.What() );
    }
}


void SIM_PLOT_FRAME::UpdateTunerValue( const SCH_SHEET_PATH& aSheetPath, const KIID& aSymbol,
                                       const wxString& aRef, const wxString& aValue )
{
    SCH_ITEM*   item = aSheetPath.GetItem( aSymbol );
    SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( item );

    if( !symbol )
    {
        DisplayErrorMessage( this, _( "Could not apply tuned value(s):" ) + wxS( " " )
                                   + wxString::Format( _( "%s not found" ), aRef ) );
        return;
    }

    SIM_LIB_MGR mgr( &Prj() );
    SIM_MODEL&  model = mgr.CreateModel( &aSheetPath, *symbol ).model;

    const SIM_MODEL::PARAM* tunerParam = model.GetTunerParam();

    if( !tunerParam )
    {
        DisplayErrorMessage( this, _( "Could not apply tuned value(s):" ) + wxS( " " )
                                   + wxString::Format( _( "%s is not tunable" ), aRef ) );
        return;
    }

    model.SetParamValue( tunerParam->info.name, std::string( aValue.ToUTF8() ) );
    model.WriteFields( symbol->GetFields() );

    m_schematicFrame->UpdateItem( symbol, false, true );
    m_schematicFrame->OnModify();
}


void SIM_PLOT_FRAME::RemoveTuner( TUNER_SLIDER* aTuner, bool aErase )
{
    if( aErase )
        m_tuners.remove( aTuner );

    aTuner->Destroy();
    m_tunePanel->Layout();
}


SIM_PLOT_PANEL* SIM_PLOT_FRAME::GetCurrentPlot() const
{
    SIM_PANEL_BASE* curPage = getCurrentPlotWindow();

    return !curPage || curPage->GetType() == ST_UNKNOWN ? nullptr
                                                        : dynamic_cast<SIM_PLOT_PANEL*>( curPage );
}


const NGSPICE_CIRCUIT_MODEL* SIM_PLOT_FRAME::GetExporter() const
{
    return m_circuitModel.get();
}


void SIM_PLOT_FRAME::addPlot( const wxString& aName, SIM_PLOT_TYPE aType )
{
    SIM_TYPE simType = m_circuitModel->GetSimType();

    if( simType == ST_UNKNOWN )
    {
        m_simConsole->AppendText( _( "Error: simulation type not defined!\n" ) );
        m_simConsole->SetInsertionPointEnd();
        return;
    }
    else if( !SIM_PANEL_BASE::IsPlottable( simType ) )
    {
        m_simConsole->AppendText( _( "Error: simulation type doesn't support plotting!\n" ) );
        m_simConsole->SetInsertionPointEnd();
        return;
    }

    // Create a new plot if the current one displays a different type
    SIM_PLOT_PANEL* plotPanel = GetCurrentPlot();

    if( !plotPanel || plotPanel->GetType() != simType )
    {
        plotPanel = dynamic_cast<SIM_PLOT_PANEL*>( NewPlotPanel( m_circuitModel->GetSimCommand(),
                                                                 m_circuitModel->GetSimOptions() ) );
    }

    wxASSERT( plotPanel );

    if( !plotPanel )    // Something is wrong
        return;

    bool updated = false;
    SIM_PLOT_TYPE xAxisType = getXAxisType( simType );

    if( xAxisType == SPT_LIN_FREQUENCY || xAxisType == SPT_LOG_FREQUENCY )
    {
        int baseType = aType & ~( SPT_AC_MAG | SPT_AC_PHASE );

        // If magnitude or phase wasn't specified, then add both
        if( baseType == aType )
        {
            updated |= updatePlot( aName, ( SIM_PLOT_TYPE )( baseType | SPT_AC_MAG ), plotPanel );
            updated |= updatePlot( aName, ( SIM_PLOT_TYPE )( baseType | SPT_AC_PHASE ), plotPanel );
        }
        else
        {
            updated |= updatePlot( aName, ( SIM_PLOT_TYPE )( aType ), plotPanel );
        }
    }
    else
    {
        updated = updatePlot( aName, aType, plotPanel );
    }

    if( updated )
        updateSignalList();
}


void SIM_PLOT_FRAME::removePlot( const wxString& aPlotName )
{
    SIM_PLOT_PANEL* plotPanel = GetCurrentPlot();

    if( !plotPanel )
        return;

    wxASSERT( plotPanel->TraceShown( aPlotName ) );
    m_workbook->DeleteTrace( plotPanel, aPlotName );
    plotPanel->GetPlotWin()->Fit();

    updateSignalList();
    wxCommandEvent dummy;
    onCursorUpdate( dummy );
}


bool SIM_PLOT_FRAME::updatePlot( const wxString& aName, SIM_PLOT_TYPE aType,
                                 SIM_PLOT_PANEL* aPlotPanel )
{
    SIM_TYPE simType = m_circuitModel->GetSimType();

    wxString plotTitle = aName;

    if( aType & SPT_AC_MAG )
        plotTitle += _( " (mag)" );
    else if( aType & SPT_AC_PHASE )
        plotTitle += _( " (phase)" );

    if( !SIM_PANEL_BASE::IsPlottable( simType ) )
    {
        // There is no plot to be shown
        m_simulator->Command( wxString::Format( wxT( "print %s" ), aName ).ToStdString() );

        return false;
    }

    // First, handle the x axis
    wxString xAxisName( m_simulator->GetXAxis( simType ) );

    if( xAxisName.IsEmpty() )
        return false;

    std::vector<double> data_x = m_simulator->GetMagPlot( (const char*) xAxisName.c_str() );
    unsigned int        size = data_x.size();

    std::vector<double> data_y;

    // Now, Y axis data
    switch( m_circuitModel->GetSimType() )
    {
    case ST_AC:
        wxASSERT_MSG( !( ( aType & SPT_AC_MAG ) && ( aType & SPT_AC_PHASE ) ),
                      wxT( "Cannot set both AC_PHASE and AC_MAG bits" ) );

        if( aType & SPT_AC_MAG )
            data_y = m_simulator->GetMagPlot( (const char*) aName.c_str() );
        else if( aType & SPT_AC_PHASE )
            data_y = m_simulator->GetPhasePlot( (const char*) aName.c_str() );
        else
            wxFAIL_MSG( wxT( "Plot type missing AC_PHASE or AC_MAG bit" ) );

        break;

    case ST_NOISE:
    case ST_DC:
    case ST_TRANSIENT:
        data_y = m_simulator->GetMagPlot( (const char*) aName.c_str() );
        break;

    default:
        wxFAIL_MSG( wxT( "Unhandled plot type" ) );
    }

    if( data_y.size() == 0 )
        return false;                   // Signal no longer exists
    else
        wxCHECK_MSG( data_y.size() >= size, false, wxT( "Not enough y data values to plot" ) );

    // If we did a two-source DC analysis, we need to split the resulting vector and add traces
    // for each input step
    SPICE_DC_PARAMS source1, source2;

    if( m_circuitModel->GetSimType() == ST_DC
        && m_circuitModel->ParseDCCommand( m_circuitModel->GetSimCommand(), &source1, &source2 ) )
    {
        if( !source2.m_source.IsEmpty() )
        {
            // Source 1 is the inner loop, so lets add traces for each Source 2 (outer loop) step
            SPICE_VALUE v = source2.m_vstart;
            wxString name;

            size_t offset = 0;
            size_t outer = ( size_t )( ( source2.m_vend - v ) / source2.m_vincrement ).ToDouble();
            size_t inner = data_x.size() / ( outer + 1 );

            wxASSERT( data_x.size() % ( outer + 1 ) == 0 );

            for( size_t idx = 0; idx <= outer; idx++ )
            {
                name = wxString::Format( wxT( "%s (%s = %s V)" ),
                                         plotTitle,
                                         source2.m_source,
                                         v.ToString() );

                std::vector<double> sub_x( data_x.begin() + offset,
                                           data_x.begin() + offset + inner );
                std::vector<double> sub_y( data_y.begin() + offset,
                                           data_y.begin() + offset + inner );

                m_workbook->AddTrace( aPlotPanel, name, aName, inner, sub_x.data(), sub_y.data(),
                                      aType );

                v = v + source2.m_vincrement;
                offset += inner;
            }

            return true;
        }
    }

    m_workbook->AddTrace( aPlotPanel, plotTitle, aName, size, data_x.data(), data_y.data(), aType );

    return true;
}


void SIM_PLOT_FRAME::updateSignalList()
{
    m_signals->ClearAll();

    SIM_PLOT_PANEL* plotPanel = GetCurrentPlot();

    if( !plotPanel )
        return;

    wxSize size = m_signals->GetClientSize();
    m_signals->AppendColumn( _( "Signal" ), wxLIST_FORMAT_LEFT, size.x );

    // Build an image list, to show the color of the corresponding trace
    // in the plot panel
    // This image list is used for trace and cursor lists
    wxMemoryDC bmDC;
    const int isize = bmDC.GetCharHeight();

    if( m_signalsIconColorList == nullptr )
        m_signalsIconColorList = new wxImageList( isize, isize, false );
    else
        m_signalsIconColorList->RemoveAll();

    for( const auto& [name, trace] : GetCurrentPlot()->GetTraces() )
    {
        wxBitmap bitmap( isize, isize );
        bmDC.SelectObject( bitmap );
        wxColour tcolor = trace->GetPen().GetColour();

        wxColour bgColor = m_signals->wxWindow::GetBackgroundColour();
        bmDC.SetPen( wxPen( bgColor ) );
        bmDC.SetBrush( wxBrush( bgColor ) );
        bmDC.DrawRectangle( 0, 0, isize, isize ); // because bmDC.Clear() does not work in wxGTK

        bmDC.SetPen( wxPen( tcolor ) );
        bmDC.SetBrush( wxBrush( tcolor ) );
        bmDC.DrawRectangle( 0, isize / 4 + 1, isize, isize / 2 );

        bmDC.SelectObject( wxNullBitmap );  // Needed to initialize bitmap

        bitmap.SetMask( new wxMask( bitmap, *wxBLACK ) );
        m_signalsIconColorList->Add( bitmap );
    }

    if( bmDC.IsOk() )
    {
        bmDC.SetBrush( wxNullBrush );
        bmDC.SetPen( wxNullPen );
    }

    m_signals->SetImageList( m_signalsIconColorList, wxIMAGE_LIST_SMALL );

    // Fill the signals listctrl. Keep the order of names and
    // the order of icon color identical, because the icons
    // are also used in cursor list, and the color index is
    // calculated from the trace name index
    int imgidx = 0;

    for( const auto& [name, trace] : plotPanel->GetTraces() )
    {
        m_signals->InsertItem( imgidx, name, imgidx );
        imgidx++;
    }
}


void SIM_PLOT_FRAME::applyTuners()
{
    wxString            errors;
    WX_STRING_REPORTER  reporter( &errors );

    for( const TUNER_SLIDER* tuner : m_tuners )
    {
        SCH_SHEET_PATH sheetPath;
        wxString       ref = tuner->GetSymbolRef();
        KIID           symbolId = tuner->GetSymbol( &sheetPath );
        SCH_ITEM*      schItem = sheetPath.GetItem( symbolId );
        SCH_SYMBOL*    symbol = dynamic_cast<SCH_SYMBOL*>( schItem );

        if( !symbol )
        {
            reporter.Report( wxString::Format( _( "%s not found" ), ref ) );
            continue;
        }

        const SPICE_ITEM* item = GetExporter()->FindItem( tuner->GetSymbolRef().ToStdString() );

        if( !item || !item->model->GetTunerParam() )
        {
            reporter.Report( wxString::Format( _( "%s is not tunable" ), ref ) );
            continue;
        }

        SIM_VALUE_FLOAT floatVal( tuner->GetValue().ToDouble() );

        m_simulator->Command( item->model->SpiceGenerator().TunerCommand( *item, floatVal ) );
    }

    if( reporter.HasMessage() )
        DisplayErrorMessage( this,
                             _( "Could not apply tuned value(s):" ) + wxS( "\n\n" ) + errors );
}


bool SIM_PLOT_FRAME::LoadWorkbook( const wxString& aPath )
{
    m_workbook->DeleteAllPages();

    wxTextFile file( aPath );

#define DISPLAY_LOAD_ERROR( fmt ) DisplayErrorMessage( this, wxString::Format( _( fmt ), \
            file.GetCurrentLine()+1 ) )

    if( !file.Open() )
        return false;

    long     version = 1;
    wxString firstLine = file.GetFirstLine();
    wxString plotCountLine;

    if( firstLine.StartsWith( wxT( "version " ) ) )
    {
        if( !firstLine.substr( 8 ).ToLong( &version ) )
        {
            DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is not an integer." );
            file.Close();

            return false;
        }

        plotCountLine = file.GetNextLine();
    }
    else
    {
        plotCountLine = firstLine;
    }

    long plotsCount;

    if( !plotCountLine.ToLong( &plotsCount ) ) // GetFirstLine instead of GetNextLine
    {
        DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is not an integer." );
        file.Close();

        return false;
    }

    for( long i = 0; i < plotsCount; ++i )
    {
        long plotType, tracesCount;

        if( !file.GetNextLine().ToLong( &plotType ) )
        {
            DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is not an integer." );
            file.Close();

            return false;
        }

        wxString          command = UnescapeString( file.GetNextLine() );
        wxString          simCommand;
        int               simOptions = NETLIST_EXPORTER_SPICE::OPTION_DEFAULT_FLAGS;
        wxStringTokenizer tokenizer( command, wxT( "\r\n" ), wxTOKEN_STRTOK );

        if( version >= 2 )
        {
            simOptions &= ~NETLIST_EXPORTER_SPICE::OPTION_ADJUST_INCLUDE_PATHS;
            simOptions &= ~NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_VOLTAGES;
            simOptions &= ~NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_CURRENTS;
        }

        while( tokenizer.HasMoreTokens() )
        {
            wxString line = tokenizer.GetNextToken();

            if( line.StartsWith( wxT( ".kicad adjustpaths" ) ) )
                simOptions |= NETLIST_EXPORTER_SPICE::OPTION_ADJUST_INCLUDE_PATHS;
            else if( line.StartsWith( wxT( ".save all" ) ) )
                simOptions |= NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_VOLTAGES;
            else if( line.StartsWith( wxT( ".probe alli" ) ) )
                simOptions |= NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_CURRENTS;
            else
                simCommand += line + wxT( "\n" );
        }

        NewPlotPanel( simCommand, simOptions );
        StartSimulation( simCommand );

        // Perform simulation, so plots can be added with values
        do
        {
            wxThread::This()->Sleep( 50 );
        }
        while( m_simulator->IsRunning() );

        if( !file.GetNextLine().ToLong( &tracesCount ) )
        {
            DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is not an integer." );
            file.Close();

            return false;
        }

        for( long j = 0; j < tracesCount; ++j )
        {
            long traceType;
            wxString name, param;

            if( !file.GetNextLine().ToLong( &traceType ) )
            {
                DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is not an integer." );
                file.Close();

                return false;
            }

            name = file.GetNextLine();

            if( name.IsEmpty() )
            {
                DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is empty." );
                file.Close();

                return false;
            }

            param = file.GetNextLine();

            #if 0   // no longer in use
            if( param.IsEmpty() )
            {
                DISPLAY_LOAD_ERROR( "Error loading workbook: Line %d is empty." );
                file.Close();

                return false;
            }
            #endif

            addPlot( name, (SIM_PLOT_TYPE) traceType );
        }
    }

    file.Close();

    wxFileName filename( aPath );
    filename.MakeRelativeTo( Prj().GetProjectPath() );

    // Remember the loaded workbook filename.
    m_simulator->Settings()->SetWorkbookFilename( filename.GetFullPath() );

    // Successfully loading a workbook does not count as modifying it.
    m_workbook->ClrModified();
    return true;
}


bool SIM_PLOT_FRAME::SaveWorkbook( const wxString& aPath )
{
    wxFileName filename = aPath;
    filename.SetExt( WorkbookFileExtension );

    wxTextFile file( filename.GetFullPath() );

    if( file.Exists() )
    {
        if( !file.Open() )
            return false;

        file.Clear();
    }
    else
    {
        file.Create();
    }

    file.AddLine( wxT( "version 2" ) );

    file.AddLine( wxString::Format( wxT( "%llu" ), m_workbook->GetPageCount() ) );

    for( size_t i = 0; i < m_workbook->GetPageCount(); i++ )
    {
        const SIM_PANEL_BASE* basePanel = dynamic_cast<const SIM_PANEL_BASE*>( m_workbook->GetPage( i ) );

        if( !basePanel )
        {
            file.AddLine( wxString::Format( wxT( "%llu" ), 0ull ) );
            continue;
        }

        file.AddLine( wxString::Format( wxT( "%d" ), basePanel->GetType() ) );

        wxString command = m_workbook->GetSimCommand( basePanel );
        int      options = m_workbook->GetSimOptions( basePanel );

        if( options & NETLIST_EXPORTER_SPICE::OPTION_ADJUST_INCLUDE_PATHS )
            command += wxT( "\n.kicad adjustpaths" );

        if( options & NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_VOLTAGES )
            command += wxT( "\n.save all" );

        if( options & NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_CURRENTS )
            command += wxT( "\n.probe alli" );

        file.AddLine( EscapeString( command, CTX_LINE ) );

        const SIM_PLOT_PANEL* plotPanel = dynamic_cast<const SIM_PLOT_PANEL*>( basePanel );

        if( !plotPanel )
        {
            file.AddLine( wxString::Format( wxT( "%llu" ), 0ull ) );
            continue;
        }

        file.AddLine( wxString::Format( wxT( "%llu" ), plotPanel->GetTraces().size() ) );

        for( const auto& [name, trace] : plotPanel->GetTraces() )
        {
            file.AddLine( wxString::Format( wxT( "%d" ), trace->GetType() ) );
            file.AddLine( trace->GetName() );
            file.AddLine( trace->GetParam().IsEmpty() ? wxS( " " ) : trace->GetParam() );
        }
    }

    bool res = file.Write();
    file.Close();

    // Store the filename of the last saved workbook.
    if( res )
    {
        filename.MakeRelativeTo( Prj().GetProjectPath() );
        m_simulator->Settings()->SetWorkbookFilename( filename.GetFullPath() );
    }

    m_workbook->ClrModified();
    return res;
}


SIM_PLOT_TYPE SIM_PLOT_FRAME::getXAxisType( SIM_TYPE aType ) const
{
    switch( aType )
    {
        /// @todo SPT_LOG_FREQUENCY
        case ST_AC:        return SPT_LIN_FREQUENCY;
        case ST_DC:        return SPT_SWEEP;
        case ST_TRANSIENT: return SPT_TIME;
        default:
            wxASSERT_MSG( false, wxT( "Unhandled simulation type" ) );
            return (SIM_PLOT_TYPE) 0;
    }
}


void SIM_PLOT_FRAME::ToggleDarkModePlots()
{
    m_darkMode = !m_darkMode;

    // Rebuild the color list to plot traces
    SIM_PLOT_COLORS::FillDefaultColorList( m_darkMode );

    // Now send changes to all SIM_PLOT_PANEL
    for( size_t page = 0; page < m_workbook->GetPageCount(); page++ )
    {
        wxWindow* curPage = m_workbook->GetPage( page );

        // ensure it is truly a plot panel and not the (zero plots) placeholder
        // which is only SIM_PLOT_PANEL_BASE
        SIM_PLOT_PANEL* panel = dynamic_cast<SIM_PLOT_PANEL*>( curPage );

        if( panel )
            panel->UpdatePlotColors();
    }
}


void SIM_PLOT_FRAME::onPlotClose( wxAuiNotebookEvent& event )
{
}


void SIM_PLOT_FRAME::onPlotClosed( wxAuiNotebookEvent& event )
{
    if( m_workbook->GetPageCount() == 0 )
    {
        m_signals->ClearAll();
        m_cursors->ClearAll();
    }
    else
    {
        updateSignalList();
        wxCommandEvent dummy;
        onCursorUpdate( dummy );
    }
}


void SIM_PLOT_FRAME::onPlotChanged( wxAuiNotebookEvent& event )
{
    updateSignalList();
    wxCommandEvent dummy;
    onCursorUpdate( dummy );
}


void SIM_PLOT_FRAME::onPlotDragged( wxAuiNotebookEvent& event )
{
}


void SIM_PLOT_FRAME::onSignalDblClick( wxMouseEvent& event )
{
    // Remove signal from the plot panel when double clicked
    long idx = m_signals->GetFocusedItem();

    if( idx != wxNOT_FOUND )
        removePlot( m_signals->GetItemText( idx, 0 ) );
}


void SIM_PLOT_FRAME::onSignalRClick( wxListEvent& aEvent )
{
    long idx = aEvent.GetIndex();

    if( idx != wxNOT_FOUND )
        m_signals->Select( idx );

    idx = m_signals->GetFirstSelected();

    if( idx != wxNOT_FOUND )
    {
        const wxString& netName = m_signals->GetItemText( idx, 0 );
        SIGNAL_CONTEXT_MENU ctxMenu( netName, this );
        m_signals->PopupMenu( &ctxMenu );
    }
}


void SIM_PLOT_FRAME::onCursorRClick( wxListEvent& aEvent )
{
    long idx = aEvent.GetIndex();

    if( idx != wxNOT_FOUND )
        m_signals->Select( idx );

    idx = m_signals->GetFirstSelected();

    if( idx != wxNOT_FOUND )
    {
        const wxString& netName = m_signals->GetItemText( idx, 0 );
        CURSOR_CONTEXT_MENU ctxMenu( netName, this );
        m_signals->PopupMenu( &ctxMenu );
    }
}


void SIM_PLOT_FRAME::onWorkbookModified( wxCommandEvent& event )
{
    updateTitle();
}


void SIM_PLOT_FRAME::onWorkbookClrModified( wxCommandEvent& event )
{
    updateTitle();
}


void SIM_PLOT_FRAME::EditSimCommand()
{
    SIM_PANEL_BASE*     plotPanelWindow = getCurrentPlotWindow();
    DIALOG_SIM_COMMAND  dlg( this, m_circuitModel, m_simulator->Settings() );
    wxString            errors;
    WX_STRING_REPORTER  reporter( &errors );

    if( !m_circuitModel->ReadSchematicAndLibraries( NETLIST_EXPORTER_SPICE::OPTION_DEFAULT_FLAGS,
                                                    reporter ) )
    {
        DisplayErrorMessage( this, _( "Errors during netlist generation; simulation aborted.\n\n" )
                                   + errors );
        return;
    }

    if( m_workbook->GetPageIndex( plotPanelWindow ) != wxNOT_FOUND )
    {
        dlg.SetSimCommand( m_workbook->GetSimCommand( plotPanelWindow ) );
        dlg.SetSimOptions( m_workbook->GetSimOptions( plotPanelWindow ) );
    }
    else
    {
        dlg.SetSimOptions( NETLIST_EXPORTER_SPICE::OPTION_DEFAULT_FLAGS );
    }

    if( dlg.ShowModal() == wxID_OK )
    {
        wxString oldCommand;

        if( m_workbook->GetPageIndex( plotPanelWindow ) != wxNOT_FOUND )
            oldCommand = m_workbook->GetSimCommand( plotPanelWindow );
        else
            oldCommand = wxString();

        const wxString& newCommand = dlg.GetSimCommand();
        int             newOptions = dlg.GetSimOptions();
        SIM_TYPE        newSimType = NGSPICE_CIRCUIT_MODEL::CommandToSimType( newCommand );

        if( !plotPanelWindow )
        {
            m_circuitModel->SetSimCommandOverride( newCommand );
            m_circuitModel->SetSimOptions( newOptions );
            plotPanelWindow = NewPlotPanel( newCommand, newOptions );
        }
        // If it is a new simulation type, open a new plot.  For the DC sim, check if sweep
        // source type has changed (char 4 will contain 'v', 'i', 'r' or 't'.
        else if( plotPanelWindow->GetType() != newSimType
                    || ( newSimType == ST_DC
                         && oldCommand.Lower().GetChar( 4 ) != newCommand.Lower().GetChar( 4 ) ) )
        {
            plotPanelWindow = NewPlotPanel( newCommand, newOptions );
        }
        else
        {
            if( m_workbook->GetPageIndex( plotPanelWindow ) == 0 )
                m_circuitModel->SetSimCommandOverride( newCommand );

            // Update simulation command in the current plot
            m_workbook->SetSimCommand( plotPanelWindow, newCommand );
            m_workbook->SetSimOptions( plotPanelWindow, newOptions );
        }

        m_simulator->Init();
    }
}


bool SIM_PLOT_FRAME::canCloseWindow( wxCloseEvent& aEvent )
{
    if( m_workbook->IsModified() )
    {
        wxFileName filename = m_simulator->Settings()->GetWorkbookFilename();

        if( filename.GetName().IsEmpty() )
        {
            if( Prj().GetProjectName().IsEmpty() )
                filename.SetFullName( wxT( "noname.wbk" ) );
            else
                filename.SetFullName( Prj().GetProjectName() + wxT( ".wbk" ) );
        }

#if 0 // TODO: Enable once 8.0 opens for dev
        wxString msg = _( "Save changes to workbook?" );
#else
        wxString fullFilename = filename.GetFullName();
        wxString msg = _( "Save changes to '%s' before closing?" );
        msg.Printf( msg, fullFilename );
#endif

        return HandleUnsavedChanges( this, msg,
                                     [&]() -> bool
                                     {
                                         return SaveWorkbook( Prj().AbsolutePath( fullFilename ) );
                                     } );
    }

    return true;
}


void SIM_PLOT_FRAME::doCloseWindow()
{
    if( m_simulator->IsRunning() )
        m_simulator->Stop();

    // Prevent memory leak on exit by deleting all simulation vectors
    m_simulator->Clean();

    // Cancel a running simProbe or simTune tool
    m_schematicFrame->GetToolManager()->RunAction( ACTIONS::cancelInteractive );

    SaveSettings( config() );

    m_simulator->Settings() = nullptr;

    Destroy();
}


void SIM_PLOT_FRAME::onCursorUpdate( wxCommandEvent& event )
{
    wxSize size = m_cursors->GetClientSize();
    SIM_PLOT_PANEL* plotPanel = GetCurrentPlot();
    m_cursors->ClearAll();

    if( !plotPanel )
        return;

    if( m_signalsIconColorList )
        m_cursors->SetImageList(m_signalsIconColorList, wxIMAGE_LIST_SMALL);

    // Fill the signals listctrl
    m_cursors->AppendColumn( _( "Signal" ), wxLIST_FORMAT_LEFT, size.x / 2 );
    const long X_COL = m_cursors->AppendColumn( plotPanel->GetLabelX(), wxLIST_FORMAT_LEFT,
                                                size.x / 4 );

    wxString labelY1 = plotPanel->GetLabelY1();
    wxString labelY2 = plotPanel->GetLabelY2();
    wxString labelY;

    if( !labelY2.IsEmpty() )
        labelY = labelY1 + wxT( " / " ) + labelY2;
    else
        labelY = labelY1;

    const long Y_COL = m_cursors->AppendColumn( labelY, wxLIST_FORMAT_LEFT, size.x / 4 );

    // Update cursor values
    int itemidx = 0;

    for( const auto& [name, trace] : plotPanel->GetTraces() )
    {
        if( CURSOR* cursor = trace->GetCursor() )
        {
            // Find the right icon color in list.
            // It is the icon used in m_signals list for the same trace
            long iconColor = m_signals->FindItem( -1, name );

            const wxRealPoint coords = cursor->GetCoords();
            long              idx = m_cursors->InsertItem( itemidx++, name, iconColor );
            m_cursors->SetItem( idx, X_COL, SPICE_VALUE( coords.x ).ToSpiceString() );
            m_cursors->SetItem( idx, Y_COL, SPICE_VALUE( coords.y ).ToSpiceString() );
        }
    }
}


void SIM_PLOT_FRAME::setupUIConditions()
{
    EDA_BASE_FRAME::setupUIConditions();

    ACTION_MANAGER*   mgr = m_toolManager->GetActionManager();
    wxASSERT( mgr );

    auto showGridCondition =
            [this]( const SELECTION& aSel )
            {
                SIM_PLOT_PANEL* plot = GetCurrentPlot();
                return plot && plot->IsGridShown();
            };

    auto showLegendCondition =
            [this]( const SELECTION& aSel )
            {
                SIM_PLOT_PANEL* plot = GetCurrentPlot();
                return plot && plot->IsLegendShown();
            };

    auto showDottedCondition =
            [this]( const SELECTION& aSel )
            {
                SIM_PLOT_PANEL* plot = GetCurrentPlot();
                return plot && plot->GetDottedSecondary();
            };

    auto darkModePlotCondition =
            [this]( const SELECTION& aSel )
            {
                return m_darkMode;
            };

    auto haveCommand =
            [this]( const SELECTION& aSel )
            {
                return m_circuitModel->CommandToSimType( GetCurrentSimCommand() ) != ST_UNKNOWN;
            };

    auto simRunning =
            [this]( const SELECTION& aSel )
            {
                return m_simulator && m_simulator->IsRunning();
            };

    auto simFinished =
            [this]( const SELECTION& aSel )
            {
                return m_simFinished;
            };

    auto havePlot =
            [this]( const SELECTION& aSel )
            {
                return GetCurrentPlot() != nullptr;
            };

#define ENABLE( x ) ACTION_CONDITIONS().Enable( x )
#define CHECK( x )  ACTION_CONDITIONS().Check( x )

    mgr->SetConditions( EE_ACTIONS::openWorkbook,          ENABLE( SELECTION_CONDITIONS::ShowAlways ) );
    mgr->SetConditions( EE_ACTIONS::saveWorkbook,          ENABLE( SELECTION_CONDITIONS::ShowAlways ) );
    mgr->SetConditions( EE_ACTIONS::saveWorkbookAs,        ENABLE( SELECTION_CONDITIONS::ShowAlways ) );

    mgr->SetConditions( EE_ACTIONS::exportPlotAsPNG,       ENABLE( havePlot ) );
    mgr->SetConditions( EE_ACTIONS::exportPlotAsCSV,       ENABLE( havePlot ) );

    mgr->SetConditions( EE_ACTIONS::toggleGrid,            CHECK( showGridCondition ) );
    mgr->SetConditions( EE_ACTIONS::toggleLegend,          CHECK( showLegendCondition ) );
    mgr->SetConditions( EE_ACTIONS::toggleDottedSecondary, CHECK( showDottedCondition ) );
    mgr->SetConditions( EE_ACTIONS::toggleDarkModePlots,   CHECK( darkModePlotCondition ) );

    mgr->SetConditions( EE_ACTIONS::simCommand,            ENABLE( SELECTION_CONDITIONS::ShowAlways ) );
    mgr->SetConditions( EE_ACTIONS::runSimulation,         ENABLE( haveCommand && !simRunning ) );
    mgr->SetConditions( EE_ACTIONS::stopSimulation,        ENABLE( simRunning ) );
    mgr->SetConditions( EE_ACTIONS::addSignals,            ENABLE( simFinished ) );
    mgr->SetConditions( EE_ACTIONS::simProbe,              ENABLE( simFinished ) );
    mgr->SetConditions( EE_ACTIONS::simTune,               ENABLE( simFinished ) );
    mgr->SetConditions( EE_ACTIONS::showNetlist,           ENABLE( SELECTION_CONDITIONS::ShowAlways ) );

#undef CHECK
#undef ENABLE
}


void SIM_PLOT_FRAME::onSimStarted( wxCommandEvent& aEvent )
{
    SetCursor( wxCURSOR_ARROWWAIT );
}


void SIM_PLOT_FRAME::onSimFinished( wxCommandEvent& aEvent )
{
    SetCursor( wxCURSOR_ARROW );

    SIM_TYPE simType = m_circuitModel->GetSimType();

    if( simType == ST_UNKNOWN )
        return;

    SIM_PANEL_BASE* plotPanelWindow = getCurrentPlotWindow();

    if( !plotPanelWindow || plotPanelWindow->GetType() != simType )
    {
        plotPanelWindow = NewPlotPanel( m_circuitModel->GetSimCommand(),
                                        m_circuitModel->GetSimOptions() );
    }

    // Sometimes (for instance with a directive like wrdata my_file.csv "my_signal")
    // the simulator is in idle state (simulation is finished), but still running, during
    // the time the file is written. So gives a slice of time to fully finish the work:
    if( m_simulator->IsRunning() )
    {
        int max_time = 40;      // For a max timeout = 2s

        do
        {
            wxMilliSleep( 50 );
            wxYield();

            if( max_time )
                max_time--;

        } while( max_time && m_simulator->IsRunning() );
    }
    // Is a warning message useful if the simulatior is still running?

    // If there are any signals plotted, update them
    if( SIM_PANEL_BASE::IsPlottable( simType ) )
    {
        SIM_PLOT_PANEL* plotPanel = dynamic_cast<SIM_PLOT_PANEL*>( plotPanelWindow );
        wxCHECK_RET( plotPanel, wxT( "not a SIM_PLOT_PANEL" ) );

        struct TRACE_DESC
        {
            wxString      m_name;    ///< Name of the measured net/device
            SIM_PLOT_TYPE m_type;    ///< Type of the signal
        };

        std::vector<struct TRACE_DESC> traceInfo;

        // Get information about all the traces on the plot, remove and add again
        for( auto& [name, trace] : plotPanel->GetTraces() )
        {
            struct TRACE_DESC placeholder;
            placeholder.m_name = trace->GetName();
            placeholder.m_type = trace->GetType();

            traceInfo.push_back( placeholder );
        }

        for( const struct TRACE_DESC& trace : traceInfo )
        {
            if( !updatePlot( trace.m_name, trace.m_type, plotPanel ) )
                removePlot( trace.m_name );
        }

        updateSignalList();
        plotPanel->GetPlotWin()->UpdateAll();
        plotPanel->ResetScales();
    }
    else if( simType == ST_OP )
    {
        m_simConsole->AppendText( _( "\n\nSimulation results:\n\n" ) );
        m_simConsole->SetInsertionPointEnd();

        for( const std::string& vec : m_simulator->AllPlots() )
        {
            std::vector<double> val_list = m_simulator->GetRealPlot( vec, 1 );

            if( val_list.size() == 0 )      // The list of values can be empty!
                continue;

            double val = val_list.at( 0 );
            wxString      outLine, signal;
            SIM_PLOT_TYPE type = m_circuitModel->VectorToSignal( vec, signal );

            const size_t tab     = 25; //characters
            size_t padding = ( signal.length() < tab ) ? ( tab - signal.length() ) : 1;

            outLine.Printf( wxT( "%s%s" ),
                            ( signal + wxT( ":" ) ).Pad( padding, wxUniChar( ' ' ) ),
                            SPICE_VALUE( val ).ToSpiceString() );

            outLine.Append( type == SPT_CURRENT ? wxT( "A\n" ) : wxT( "V\n" ) );

            m_simConsole->AppendText( outLine );
            m_simConsole->SetInsertionPointEnd();

            // @todo display calculated values on the schematic
        }
    }

    m_lastSimPlot = plotPanelWindow;
    m_simFinished = true;
}


void SIM_PLOT_FRAME::onSimUpdate( wxCommandEvent& aEvent )
{
    static bool updateInProgress = false;

    // skip update when events are triggered too often and previous call didn't end yet
    if( updateInProgress )
        return;

    updateInProgress = true;

    if( m_simulator->IsRunning() )
        m_simulator->Stop();

    if( getCurrentPlotWindow() != m_lastSimPlot )
    {
        // We need to rerun simulation, as the simulator currently stores
        // results for another plot
        StartSimulation();
    }
    else
    {
        std::unique_lock<std::mutex> simulatorLock( m_simulator->GetMutex(), std::try_to_lock );

        if( simulatorLock.owns_lock() )
        {
            // Incremental update
            m_simConsole->Clear();

            // Do not export netlist, it is already stored in the simulator
            applyTuners();

            m_simulator->Run();
        }
        else
        {
            DisplayErrorMessage( this, _( "Another simulation is already running." ) );
        }
    }
    updateInProgress = false;
}


void SIM_PLOT_FRAME::onSimReport( wxCommandEvent& aEvent )
{
    m_simConsole->AppendText( aEvent.GetString() + "\n" );
    m_simConsole->SetInsertionPointEnd();
}


SIM_PLOT_FRAME::SIGNAL_CONTEXT_MENU::SIGNAL_CONTEXT_MENU( const wxString& aSignal,
                                                          SIM_PLOT_FRAME* aPlotFrame ) :
        m_signal( aSignal ),
        m_plotFrame( aPlotFrame )
{
    SIM_PLOT_PANEL* plot = m_plotFrame->GetCurrentPlot();

    AddMenuItem( this, REMOVE_SIGNAL, _( "Remove Signal" ), _( "Remove the signal from the plot" ),
                 KiBitmap( BITMAPS::trash ) );

    TRACE* trace = plot->GetTrace( m_signal );

    AppendSeparator();

    if( trace->HasCursor() )
        AddMenuItem( this, HIDE_CURSOR, _( "Hide Cursor" ), KiBitmap( BITMAPS::pcb_target ) );
    else
        AddMenuItem( this, SHOW_CURSOR, _( "Show Cursor" ), KiBitmap( BITMAPS::pcb_target ) );

    Connect( wxEVT_COMMAND_MENU_SELECTED, wxMenuEventHandler( SIGNAL_CONTEXT_MENU::onMenuEvent ),
             nullptr, this );
}


void SIM_PLOT_FRAME::SIGNAL_CONTEXT_MENU::onMenuEvent( wxMenuEvent& aEvent )
{
    SIM_PLOT_PANEL* plot = m_plotFrame->GetCurrentPlot();

    switch( aEvent.GetId() )
    {
    case REMOVE_SIGNAL: m_plotFrame->removePlot( m_signal );   break;
    case SHOW_CURSOR:   plot->EnableCursor( m_signal, true );  break;
    case HIDE_CURSOR:   plot->EnableCursor( m_signal, false ); break;
    }
}


SIM_PLOT_FRAME::CURSOR_CONTEXT_MENU::CURSOR_CONTEXT_MENU( const wxString& aSignal,
                                                          SIM_PLOT_FRAME* aPlotFrame ) :
        m_signal( aSignal ),
        m_plotFrame( aPlotFrame )
{
    AddMenuItem( this, HIDE_CURSOR, _( "Hide Cursor" ), KiBitmap( BITMAPS::pcb_target ) );

    Connect( wxEVT_COMMAND_MENU_SELECTED, wxMenuEventHandler( CURSOR_CONTEXT_MENU::onMenuEvent ),
             nullptr, this );
}


void SIM_PLOT_FRAME::CURSOR_CONTEXT_MENU::onMenuEvent( wxMenuEvent& aEvent )
{
    SIM_PLOT_PANEL* plot = m_plotFrame->GetCurrentPlot();

    if( aEvent.GetId() == HIDE_CURSOR )
        plot->EnableCursor( m_signal, false );
}


void SIM_PLOT_FRAME::onExit( wxCommandEvent& event )
{
    Kiway().OnKiCadExit();
}


wxDEFINE_EVENT( EVT_SIM_UPDATE, wxCommandEvent );
wxDEFINE_EVENT( EVT_SIM_REPORT, wxCommandEvent );

wxDEFINE_EVENT( EVT_SIM_STARTED, wxCommandEvent );
wxDEFINE_EVENT( EVT_SIM_FINISHED, wxCommandEvent );
