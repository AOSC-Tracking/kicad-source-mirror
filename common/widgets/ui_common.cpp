/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <wx/wx.h>
#include <widgets/ui_common.h>

#include <algorithm>
#include <gal/color4d.h>

int KIUI::GetStdMargin()
{
    // This is the value used in (most) wxFB dialogs
    return 5;
}


#define BADGE_SIZE       24
#define BADGE_FONT_SIZE  10

wxBitmap MakeBadge( SEVERITY aStyle, int aCount, wxWindow* aWindow, int aDepth )
{
    wxSize      size( BADGE_SIZE, BADGE_SIZE );
    wxBitmap    bitmap( size );
    wxBrush     brush;
    wxMemoryDC  badgeDC;
    wxColour    badgeColour;
    wxColour    textColour;
    wxColour    backColour;
    int         fontSize = BADGE_FONT_SIZE;

    if( aCount > 99 )
        fontSize--;

    badgeDC.SelectObject( bitmap );

    brush.SetStyle( wxBRUSHSTYLE_SOLID );

    backColour = aWindow->GetParent()->GetBackgroundColour();

    // Each level inside staticBoxes is darkened by 215
    for( int i = 1; i < aDepth; ++i )
        backColour = backColour.MakeDisabled( 215 );

    brush.SetColour( backColour );
    badgeDC.SetBackground( brush );
    badgeDC.Clear();

    if( aCount < 0 )
    {
        return bitmap;
    }
    else if( aCount == 0 )
    {
        if( aStyle == RPT_SEVERITY_ERROR || aStyle == RPT_SEVERITY_WARNING )
        {
            badgeColour = KIGFX::COLOR4D( GREEN ).ToColour();
            textColour = *wxWHITE;
        }
        else
        {
            return bitmap;
        }
    }
    else
    {
        switch( aStyle )
        {
        case RPT_SEVERITY_ERROR:
            badgeColour = *wxRED;
            textColour = *wxWHITE;
            break;
        case RPT_SEVERITY_WARNING:
            badgeColour = *wxYELLOW;
            textColour = *wxBLACK;
            break;
        case RPT_SEVERITY_ACTION:
            badgeColour = KIGFX::COLOR4D( GREEN ).ToColour();
            textColour = *wxWHITE;
            break;
        case RPT_SEVERITY_EXCLUSION:
        case RPT_SEVERITY_INFO:
        default:
            badgeColour = *wxLIGHT_GREY;
            textColour = *wxBLACK;
            break;
        }
    }

    brush.SetStyle( wxBRUSHSTYLE_SOLID );
    brush.SetColour( badgeColour );
    badgeDC.SetBrush( brush );
    badgeDC.SetPen( wxPen( badgeColour, 0 ) );
    badgeDC.DrawCircle( size.x / 2 - 1, size.y / 2, ( std::max( size.x, size.y ) / 2 ) - 1 );

    wxFont font( fontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD );
    badgeDC.SetFont( font );

    wxString text = wxString::Format( wxT( "%d" ), aCount );
    wxSize textExtent = badgeDC.GetTextExtent( text );

    badgeDC.SetTextForeground( textColour );
    badgeDC.DrawText( text, size.x / 2 - textExtent.x / 2 - 1, size.y / 2 - textExtent.y / 2 );

    return bitmap;
}


SEVERITY SeverityFromString( const wxString& aSeverity )
{
    if( aSeverity == wxT( "warning" ) )
        return RPT_SEVERITY_WARNING;
    else if( aSeverity == wxT( "ignore" ) )
        return RPT_SEVERITY_IGNORE;
    else
        return RPT_SEVERITY_ERROR;
}


wxString SeverityToString( const SEVERITY& aSeverity )
{
    if( aSeverity == RPT_SEVERITY_IGNORE )
        return wxT( "ignore" );
    else if( aSeverity == RPT_SEVERITY_WARNING )
        return wxT( "warning" );
    else
        return wxT( "error" );
}
