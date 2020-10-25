/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2009 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 1992-2020 KiCad Developers, see AUTHORS.txt for contributors.
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
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file sch_junction.cpp
 */

#include <sch_draw_panel.h>
#include <trigo.h>
#include <common.h>
#include <plotter.h>
#include <bitmaps.h>
#include <macros.h>

#include <sch_painter.h>
#include <sch_junction.h>
#include <sch_connection.h>
#include <schematic.h>
#include <settings/color_settings.h>


SCH_JUNCTION::SCH_JUNCTION( const wxPoint& aPosition, int aDiameter, SCH_LAYER_ID aLayer ) :
    SCH_ITEM( NULL, SCH_JUNCTION_T )
{
    m_pos   = aPosition;
    m_color = COLOR4D::UNSPECIFIED;
    m_diameter = aDiameter;
    m_Layer = aLayer;
}


EDA_ITEM* SCH_JUNCTION::Clone() const
{
    return new SCH_JUNCTION( *this );
}


void SCH_JUNCTION::SwapData( SCH_ITEM* aItem )
{
    wxCHECK_RET( (aItem != NULL) && (aItem->Type() == SCH_JUNCTION_T),
                 wxT( "Cannot swap junction data with invalid item." ) );

    SCH_JUNCTION* item = (SCH_JUNCTION*) aItem;
    std::swap( m_pos, item->m_pos );
    std::swap( m_diameter, item->m_diameter );
    std::swap( m_color, item->m_color );
}


void SCH_JUNCTION::ViewGetLayers( int aLayers[], int& aCount ) const
{
    aCount     = 2;
    aLayers[0] = m_Layer;
    aLayers[1] = LAYER_SELECTION_SHADOWS;
}


const EDA_RECT SCH_JUNCTION::GetBoundingBox() const
{
    EDA_RECT rect;

    int size = Schematic() ? Schematic()->Settings().m_JunctionSize
                           : Mils2iu( DEFAULT_JUNCTION_DIAM );

    if( m_diameter != 0 )
        size = m_diameter;

    rect.SetOrigin( m_pos );
    rect.Inflate( ( GetPenWidth() + size ) / 2 );

    return rect;
}


void SCH_JUNCTION::Print( RENDER_SETTINGS* aSettings, const wxPoint& aOffset )
{
    wxDC*   DC    = aSettings->GetPrintDC();
    COLOR4D color = GetColor();

    if( color == COLOR4D::UNSPECIFIED )
        color = aSettings->GetLayerColor( GetLayer() );

    int diameter = Schematic() ? Schematic()->Settings().m_JunctionSize
                               : Mils2iu( DEFAULT_JUNCTION_DIAM );

    if( m_diameter != 0 )
        diameter = m_diameter;

    GRFilledCircle( nullptr, DC, m_pos.x + aOffset.x, m_pos.y + aOffset.y, diameter / 2, 0,
                    color, color );
}


void SCH_JUNCTION::MirrorX( int aXaxis_position )
{
    MIRROR( m_pos.y, aXaxis_position );
}


void SCH_JUNCTION::MirrorY( int aYaxis_position )
{
    MIRROR( m_pos.x, aYaxis_position );
}


void SCH_JUNCTION::Rotate( wxPoint aPosition )
{
    RotatePoint( &m_pos, aPosition, 900 );
}


void SCH_JUNCTION::GetEndPoints( std::vector <DANGLING_END_ITEM>& aItemList )
{
    DANGLING_END_ITEM item( JUNCTION_END, this, m_pos );
    aItemList.push_back( item );
}


std::vector<wxPoint> SCH_JUNCTION::GetConnectionPoints() const
{
    return { m_pos };
}


#if defined(DEBUG)
void SCH_JUNCTION::Show( int nestLevel, std::ostream& os ) const
{
    // XML output:
    wxString s = GetClass();

    NestedSpace( nestLevel, os ) << '<' << s.Lower().mb_str() << m_pos << ", " << m_diameter
                                 << "/>\n";
}
#endif


COLOR4D SCH_JUNCTION::GetColor() const
{
    if( m_color != COLOR4D::UNSPECIFIED )
        return m_color;

    NETCLASSPTR netclass = NetClass();

    if( netclass )
        return netclass->GetSchematicColor();

    return COLOR4D::UNSPECIFIED;
}


int SCH_JUNCTION::GetDiameter() const
{
    int diameter = m_diameter;

    if( diameter == 0 )
    {
        // Careful; preview items don't have schematics...
        if( Schematic() )
            diameter = Schematic()->Settings().m_JunctionSize;
        else
            diameter = DEFAULT_JUNCTION_DIAM * IU_PER_MILS;
    }

    NETCLASSPTR netclass = NetClass();

    if( netclass )
        diameter = std::max( diameter, KiROUND( netclass->GetWireWidth() * 1.7 ) );

    return std::max( diameter, 1 );
}


bool SCH_JUNCTION::HitTest( const wxPoint& aPosition, int aAccuracy ) const
{
    EDA_RECT rect = GetBoundingBox();

    rect.Inflate( aAccuracy );

    return rect.Contains( aPosition );
}


bool SCH_JUNCTION::HitTest( const EDA_RECT& aRect, bool aContained, int aAccuracy ) const
{
    if( m_Flags & STRUCT_DELETED || m_Flags & SKIP_STRUCT )
        return false;

    EDA_RECT rect = aRect;

    rect.Inflate( aAccuracy );

    if( aContained )
        return rect.Contains( GetBoundingBox() );

    return rect.Intersects( GetBoundingBox() );
}


bool SCH_JUNCTION::doIsConnected( const wxPoint& aPosition ) const
{
    return m_pos == aPosition;
}


void SCH_JUNCTION::Plot( PLOTTER* aPlotter )
{
    auto*   settings = static_cast<KIGFX::SCH_RENDER_SETTINGS*>( aPlotter->RenderSettings() );
    COLOR4D color = GetColor();

    if( color == COLOR4D::UNSPECIFIED )
        color = settings->GetLayerColor( GetLayer() );

    aPlotter->SetColor( color );

    int diameter = Schematic() ? Schematic()->Settings().m_JunctionSize
                               : Mils2iu( DEFAULT_JUNCTION_DIAM );

    if( m_diameter != 0 )
        diameter = m_diameter;

    aPlotter->Circle( m_pos, diameter, FILL_TYPE::FILLED_SHAPE );
}


BITMAP_DEF SCH_JUNCTION::GetMenuImage() const
{
    return add_junction_xpm;
}


bool SCH_JUNCTION::operator <( const SCH_ITEM& aItem ) const
{
    if( Type() != aItem.Type() )
        return Type() < aItem.Type();

    if( GetLayer() != aItem.GetLayer() )
        return GetLayer() < aItem.GetLayer();

    auto junction = static_cast<const SCH_JUNCTION*>( &aItem );

    if( GetPosition().x != junction->GetPosition().x )
        return GetPosition().x < junction->GetPosition().x;

    if( GetPosition().y != junction->GetPosition().y )
        return GetPosition().y < junction->GetPosition().y;

    if( GetDiameter() != junction->GetDiameter() )
        return GetDiameter() < junction->GetDiameter();

    return GetColor() < junction->GetColor();
}

