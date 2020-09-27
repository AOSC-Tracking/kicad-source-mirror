/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004-2020 KiCad Developers.
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

#include <common.h>
#include <class_board.h>
#include <class_drawsegment.h>
#include <class_pad.h>

#include <convert_basic_shapes_to_polygon.h>
#include <geometry/polygon_test_point_inside.h>
#include <geometry/seg.h>
#include <geometry/shape_poly_set.h>
#include <geometry/shape_rect.h>
#include <geometry/shape_segment.h>

#include <drc/drc_engine.h>
#include <drc/drc_item.h>
#include <drc/drc_rule.h>
#include <drc/drc_test_provider_clearance_base.h>

#include <drc/drc_rtree.h>

/*
    Silk to pads clearance test. Check all pads against silkscreen (mask opening in the pad vs silkscreen)
    Errors generated:
    - DRCE_SILK_ON_PADS

    TODO:
    - tester only looks for edge crossings. it doesn't check if items are inside/outside the board area.
*/

namespace test {

class DRC_TEST_PROVIDER_SILK_TO_PAD : public ::DRC_TEST_PROVIDER
{
public:
    DRC_TEST_PROVIDER_SILK_TO_PAD ()
    {
    }

    virtual ~DRC_TEST_PROVIDER_SILK_TO_PAD() 
    {
    }

    virtual bool Run() override;

    virtual const wxString GetName() const override 
    {
        return "silk_to_pad"; 
    };

    virtual const wxString GetDescription() const override
    {
        return "Tests for silkscreen covering components pads";
    }

    virtual int GetNumPhases() const override
    {
        return 1;
    }

    virtual std::set<DRC_CONSTRAINT_TYPE_T> GetConstraintTypes() const override;

private:

    BOARD* m_board;
    int m_largestClearance;
};

};


bool test::DRC_TEST_PROVIDER_SILK_TO_PAD::Run()
{
    m_board = m_drcEngine->GetBoard();

    DRC_CONSTRAINT worstClearanceConstraint;
    m_largestClearance = 0;

    if( m_drcEngine->QueryWorstConstraint( DRC_CONSTRAINT_TYPE_SILK_TO_PAD,
                                           worstClearanceConstraint, DRCCQ_LARGEST_MINIMUM ) )
    {
        m_largestClearance = worstClearanceConstraint.m_Value.Min();
    }

    reportAux( "Worst clearance : %d nm", m_largestClearance );
    reportPhase(( "Pad to silkscreen clearances..." ));

    struct SHAPE_ON_LAYER
    {
        SHAPE_ON_LAYER( std::shared_ptr<SHAPE> aShape, PCB_LAYER_ID aLayer )
            : shape ( aShape ),
              layer ( aLayer )
        {
        }
        std::shared_ptr<SHAPE> shape;
        PCB_LAYER_ID layer;
    };

    DRC_RTREE padTree;

    auto addPadToTree =
            [&padTree]( BOARD_ITEM *item ) -> bool
            {
                padTree.insert( item );
                return true;
            };


    auto checkClearance =
            [&] ( BOARD_ITEM* refItem ) -> bool
            {
                const PCB_LAYER_ID padOuterLayers[] = { F_Cu, B_Cu };
                const PCB_LAYER_ID padSilkLayers[] = { F_SilkS, B_SilkS };


                for( int i = 0; i < 2; i++ )
                {
                    padTree.QueryColliding(
                            refItem,
                            padSilkLayers[i],
                            padOuterLayers[i],
                            [&] ( BOARD_ITEM *chkItem ) -> bool
                            {
                                return true;
                            },
                            [&] ( BOARD_ITEM *collItem, int distance ) -> bool
                            {
                                auto constraint = m_drcEngine->EvalRulesForItems( DRC_CONSTRAINT_TYPE_SILK_TO_PAD,
                                                  refItem, collItem );

                                int  minClearance = constraint.GetValue().Min();

                                if( !distance || distance < minClearance )
                                {
                                    std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_SILK_OVER_PAD );
                                    wxString msg;

                                    msg.Printf( drcItem->GetErrorText() + _( " (%s clearance %s; actual %s)" ),
                                                constraint.GetParentRule()->m_Name,
                                                MessageTextFromValue( userUnits(), minClearance, true ),
                                                MessageTextFromValue( userUnits(), distance, true ) );

                                    drcItem->SetErrorMessage( msg );
                                    drcItem->SetItems( refItem, collItem );
                                    drcItem->SetViolatingRule( constraint.GetParentRule() );

                                    reportViolation( drcItem, refItem->GetPosition() ); 
                                }
                                return true;
                            },
                            m_largestClearance
                    );
                }

                return true;
            };

    int numPads = forEachGeometryItem( { PCB_PAD_T }, LSET::AllTechMask() | LSET::AllCuMask(), addPadToTree );

    int numSilk = forEachGeometryItem( { PCB_LINE_T, PCB_MODULE_EDGE_T, PCB_TEXT_T, PCB_MODULE_TEXT_T },
                         LSET( 2, F_SilkS, B_SilkS ),
                         checkClearance
                          );

        
    reportAux( "Tested %d pads against %d silkscreen features.", numPads, numSilk );

    
    
#if 0


    for( auto& silkShape : boardOutline )
    {
        for( auto& padShape : boardItems )
        {
//            printf("BoardT %d\n", boardItem->Type() );
            
            auto shape = boardItem->GetEffectiveShape();

            auto constraint = m_drcEngine->EvalRulesForItems( DRC_CONSTRAINT_TYPE_EDGE_CLEARANCE,
                                                              outlineItem, boardItem );
            int  minClearance = constraint.GetValue().Min();
            int  actual;

            if( refShape->Collide( shape.get(), minClearance, &actual ) )
            {
                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_COPPER_EDGE_CLEARANCE );
                wxString msg;

                msg.Printf( drcItem->GetErrorText() + _( " (%s clearance %s; actual %s)" ),
                            rule->GetName(),
                            MessageTextFromValue( userUnits(), minClearance, true ),
                            MessageTextFromValue( userUnits(), actual, true ) );

                drcItem->SetErrorMessage( msg );
                drcItem->SetItems( outlineItem, boardItem );
                drcItem->SetViolatingRule( rule );

                reportViolation( drcItem, refShape->Centre());
            }
        }
    }

    return true;

#endif

    return true;
}


std::set<DRC_CONSTRAINT_TYPE_T> test::DRC_TEST_PROVIDER_SILK_TO_PAD::GetConstraintTypes() const
{
    return { DRC_CONSTRAINT_TYPE_SILK_TO_PAD };
}


namespace detail
{
    static DRC_REGISTER_TEST_PROVIDER<test::DRC_TEST_PROVIDER_SILK_TO_PAD> dummy;
}