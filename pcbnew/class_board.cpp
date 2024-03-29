/**
 * @file class_board.cpp
 * @brief  BOARD class functions.
 */

/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2011 Wayne Stambaugh <stambaughw@verizon.net>
 *
 * Copyright (C) 1992-2017 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <limits.h>
#include <algorithm>

#include <fctsys.h>
#include <common.h>
#include <kicad_string.h>
#include <wxBasePcbFrame.h>
#include <msgpanel.h>
#include <pcb_netlist.h>
#include <reporter.h>
#include <base_units.h>
#include <ratsnest_data.h>
#include <ratsnest_viewitem.h>
#include <worksheet_viewitem.h>

#include <pcbnew.h>
#include <colors_selection.h>
#include <collectors.h>

#include <class_board.h>
#include <class_module.h>
#include <class_track.h>
#include <class_zone.h>
#include <class_marker_pcb.h>
#include <class_drawsegment.h>
#include <class_pcb_text.h>
#include <class_mire.h>
#include <class_dimension.h>
#include <connectivity.h>


/* This is an odd place for this, but CvPcb won't link if it is
 *  in class_board_item.cpp like I first tried it.
 */
wxPoint BOARD_ITEM::ZeroOffset( 0, 0 );


BOARD::BOARD() :
    BOARD_ITEM_CONTAINER( (BOARD_ITEM*) NULL, PCB_T ),
    m_NetInfo( this ),
    m_paper( PAGE_INFO::A4 )
{
    // we have not loaded a board yet, assume latest until then.
    m_fileFormatVersionAtLoad = LEGACY_BOARD_FILE_VERSION;

    m_Status_Pcb    = 0;                    // Status word: bit 1 = calculate.
    SetColorsSettings( &g_ColorsSettings );

    m_CurrentZoneContour = NULL;            // This ZONE_CONTAINER handle the
                                            // zone contour currently in progress

    BuildListOfNets();                      // prepare pad and netlist containers.

    for( LAYER_NUM layer = 0; layer < PCB_LAYER_ID_COUNT; ++layer )
    {
        m_Layer[layer].m_name = GetStandardLayerName( ToLAYER_ID( layer ) );

        if( IsCopperLayer( layer ) )
            m_Layer[layer].m_type = LT_SIGNAL;
        else
            m_Layer[layer].m_type = LT_UNDEFINED;
    }

    // Initialize default netclass.
    NETCLASSPTR defaultClass = m_designSettings.GetDefault();
    defaultClass->SetDescription( _( "This is the default net class." ) );
    m_designSettings.SetCurrentNetClass( defaultClass->GetName() );

    // Set sensible initial values for custom track width & via size
    m_designSettings.UseCustomTrackViaSize( false );
    m_designSettings.SetCustomTrackWidth( m_designSettings.GetCurrentTrackWidth() );
    m_designSettings.SetCustomViaSize( m_designSettings.GetCurrentViaSize() );
    m_designSettings.SetCustomViaDrill( m_designSettings.GetCurrentViaDrill() );

    // Initialize ratsnest
    m_connectivity.reset( new CONNECTIVITY_DATA() );
    m_connectivity->Build( this );
}


BOARD::~BOARD()
{
    while( m_ZoneDescriptorList.size() )
    {
        ZONE_CONTAINER* area_to_remove = m_ZoneDescriptorList[0];
        Delete( area_to_remove );
    }

    DeleteMARKERs();
    DeleteZONEOutlines();

    delete m_CurrentZoneContour;
    m_CurrentZoneContour = NULL;
}


const wxPoint& BOARD::GetPosition() const
{
    wxLogWarning( wxT( "This should not be called on the BOARD object") );

    return ZeroOffset;
}

void BOARD::SetPosition( const wxPoint& aPos )
{
    wxLogWarning( wxT( "This should not be called on the BOARD object") );
}


void BOARD::Move( const wxPoint& aMoveVector )        // overload
{
    // @todo : anything like this elsewhere?  maybe put into GENERAL_COLLECTOR class.
    static const KICAD_T top_level_board_stuff[] = {
        PCB_MARKER_T,
        PCB_TEXT_T,
        PCB_LINE_T,
        PCB_DIMENSION_T,
        PCB_TARGET_T,
        PCB_VIA_T,
        PCB_TRACE_T,
        //        PCB_PAD_T,            Can't be at board level
        //        PCB_MODULE_TEXT_T,    Can't be at board level
        PCB_MODULE_T,
        PCB_ZONE_AREA_T,
        EOT
    };

    INSPECTOR_FUNC inspector = [&] ( EDA_ITEM* item, void* testData )
    {
        BOARD_ITEM* brd_item = (BOARD_ITEM*) item;

        // aMoveVector was snapshotted, don't need "data".
        brd_item->Move( aMoveVector );

        return SEARCH_CONTINUE;
    };

    Visit( inspector, NULL, top_level_board_stuff );
}


TRACKS BOARD::TracksInNet( int aNetCode )
{
    TRACKS ret;

    INSPECTOR_FUNC inspector = [aNetCode,&ret] ( EDA_ITEM* item, void* testData )
    {
        TRACK*  t = (TRACK*) item;

        if( t->GetNetCode() == aNetCode )
            ret.push_back( t );

        return SEARCH_CONTINUE;
    };

    // visit this BOARD's TRACKs and VIAs with above TRACK INSPECTOR which
    // appends all in aNetCode to ret.
    Visit( inspector, NULL, GENERAL_COLLECTOR::Tracks  );

    return ret;
}


/**
 * Function removeTrack
 * removes aOneToRemove from aList, which is a non-owning std::vector
 */
static void removeTrack( TRACKS* aList, TRACK* aOneToRemove )
{
    aList->erase( std::remove( aList->begin(), aList->end(), aOneToRemove ), aList->end() );
}


static void otherEnd( const TRACK& aTrack, const wxPoint& aNotThisEnd, wxPoint* aOtherEnd )
{
    if( aTrack.GetStart() == aNotThisEnd )
    {
        *aOtherEnd = aTrack.GetEnd();
    }
    else
    {
        wxASSERT( aTrack.GetEnd() == aNotThisEnd );
        *aOtherEnd = aTrack.GetStart();
    }
}


/**
 * Function find_vias_and_tracks_at
 * collects TRACKs and VIAs at aPos and returns the @a track_count which excludes vias.
 */
static int find_vias_and_tracks_at( TRACKS& at_next, TRACKS& in_net, LSET& lset, const wxPoint& next )
{
    // first find all vias (in this net) at 'next' location, and expand LSET with each
    for( TRACKS::iterator it = in_net.begin(); it != in_net.end();  )
    {
        TRACK* t = *it;

        if( t->Type() == PCB_VIA_T && (t->GetLayerSet() & lset).any() &&
            ( t->GetStart() == next || t->GetEnd() == next ) )
        {
            lset |= t->GetLayerSet();
            at_next.push_back( t );
            it = in_net.erase( it );
        }
        else
            ++it;
    }

    int track_count = 0;

    // with expanded lset, find all tracks with an end on any of the layers in lset
    for( TRACKS::iterator it = in_net.begin();  it != in_net.end(); /* iterates in the loop body */ )
    {
        TRACK* t = *it;

        if( ( t->GetLayerSet() & lset ).any() && ( t->GetStart() == next || t->GetEnd() == next ) )
        {
            at_next.push_back( t );
            it = in_net.erase( it );
            ++track_count;
        }
        else
        {
            ++it;
        }
    }

    return track_count;
}


/**
 * Function checkConnectedTo
 * returns if aTracksInNet contains a copper pathway to aGoal when starting with
 * aFirstTrack.  aFirstTrack should have one end situated on aStart, and the
 * traversal testing begins from the other end of aFirstTrack.
 * <p>
 * The function throws an exception instead of returning bool so that detailed
 * information can be provided about a possible failure in the track layout.
 *
 * @throw IO_ERROR - if points are not connected, with text saying why.
 */
static void checkConnectedTo( BOARD* aBoard, TRACKS* aList, const TRACKS& aTracksInNet,
        const wxPoint& aGoal, const wxPoint& aStart, TRACK* aFirstTrack )
{
    TRACKS  in_net = aTracksInNet;      // copy source list so the copy can be modified
    wxPoint next;

    otherEnd( *aFirstTrack, aStart, &next );

    aList->push_back( aFirstTrack );
    removeTrack( &in_net, aFirstTrack );

    LSET lset( aFirstTrack->GetLayer() );

    while( in_net.size() )
    {
        if( next == aGoal )
            return;             // success

        // Want an exact match on the position of next, i.e. pad at next,
        // not a forgiving HitTest() with tolerance type of match, otherwise the overall
        // algorithm will not work.  GetPadFast() is an exact match as I write this.
        if( aBoard->GetPadFast( next, lset ) )
        {
            std::string m = StrPrintf(
                "intervening pad at:(xy %s) between start:(xy %s) and goal:(xy %s)",
                BOARD_ITEM::FormatInternalUnits( next ).c_str(),
                BOARD_ITEM::FormatInternalUnits( aStart ).c_str(),
                BOARD_ITEM::FormatInternalUnits( aGoal ).c_str()
                );
            THROW_IO_ERROR( m );
        }

        int track_count = find_vias_and_tracks_at( *aList, in_net, lset, next );

        if( track_count != 1 )
        {
            std::string m = StrPrintf(
                "found %d tracks intersecting at (xy %s), exactly 2 would be acceptable.",
                track_count + aList->size() == 1 ? 1 : 0,
                BOARD_ITEM::FormatInternalUnits( next ).c_str()
                );
            THROW_IO_ERROR( m );
        }

        // reduce lset down to the layer that the last track at 'next' is on.
        lset = aList->back()->GetLayerSet();

        otherEnd( *aList->back(), next, &next );
    }

    std::string m = StrPrintf(
        "not enough tracks connecting start:(xy %s) and goal:(xy %s).",
        BOARD_ITEM::FormatInternalUnits( aStart ).c_str(),
        BOARD_ITEM::FormatInternalUnits( aGoal ).c_str()
        );
    THROW_IO_ERROR( m );
}


TRACKS BOARD::TracksInNetBetweenPoints( const wxPoint& aStartPos, const wxPoint& aGoalPos, int aNetCode )
{
    TRACKS  in_between_pts;
    TRACKS  on_start_point;
    TRACKS  in_net = TracksInNet( aNetCode );   // a small subset of TRACKs and VIAs

    for( auto t : in_net )
    {
        if( t->Type() == PCB_TRACE_T && ( t->GetStart() == aStartPos || t->GetEnd() == aStartPos )  )
            on_start_point.push_back( t );
    }

    wxString  per_path_problem_text;

    for( auto t : on_start_point )    // explore each trace (path) leaving aStartPos
    {
        // checkConnectedTo() fills in_between_pts on every attempt.  For failures
        // this set needs to be cleared.
        in_between_pts.clear();

        try
        {
            checkConnectedTo( this, &in_between_pts, in_net, aGoalPos, aStartPos, t );
        }
        catch( const IO_ERROR& ioe )    // means not connected
        {
            per_path_problem_text += "\n\t";
            per_path_problem_text += ioe.Problem();
            continue;           // keep trying, there may be other paths leaving from aStartPos
        }

        // success, no exception means a valid connection,
        // return this set of TRACKS without throwing.
        return in_between_pts;
    }

    wxString m = wxString::Format(
            "no clean path connecting start:(xy %s) with goal:(xy %s)",
            BOARD_ITEM::FormatInternalUnits( aStartPos ).c_str(),
            BOARD_ITEM::FormatInternalUnits( aGoalPos ).c_str()
            );

    THROW_IO_ERROR( m + per_path_problem_text );
}


void BOARD::chainMarkedSegments( wxPoint aPosition, const LSET& aLayerSet, TRACKS* aList )
{
    LSET    layer_set = aLayerSet;

    if( !m_Track )      // no tracks at all in board
        return;

    /* Set the BUSY flag of all connected segments, first search starting at
     * aPosition.  The search ends when a pad is found (end of a track), a
     * segment end has more than one other segment end connected, or when no
     * connected item found.
     *
     * Vias are a special case because they must look for segments connected
     * on other layers and they change the layer mask.  They can be a track
     * end or not.  They will be analyzer later and vias on terminal points
     * of the track will be considered as part of this track if they do not
     * connect segments of another track together and will be considered as
     * part of an other track when removing the via, the segments of that other
     * track are disconnected.
     */
    for( ; ; )
    {


        if( GetPad( aPosition, layer_set ) != NULL )
            return;

        /* Test for a via: a via changes the layer mask and can connect a lot
         * of segments at location aPosition. When found, the via is just
         * pushed in list.  Vias will be examined later, when all connected
         * segment are found and push in list.  This is because when a via
         * is found we do not know at this time the number of connected items
         * and we do not know if this via is on the track or finish the track
         */
        TRACK* via = m_Track->GetVia( NULL, aPosition, layer_set );

        if( via )
        {
            layer_set = via->GetLayerSet();

            aList->push_back( via );
        }

        int     seg_count = 0;
        TRACK*  candidate = NULL;

        /* Search all segments connected to point aPosition.
         *  if only 1 segment at aPosition: then this segment is "candidate"
         *  if > 1 segment:
         *      then end of "track" (because more than 2 segments are connected at aPosition)
         */
        TRACK*  segment = m_Track;

        while( ( segment = ::GetTrack( segment, NULL, aPosition, layer_set ) ) != NULL )
        {
            if( segment->GetState( BUSY ) ) // already found and selected: skip it
            {
                segment = segment->Next();
                continue;
            }

            if( segment == via )    // just previously found: skip it
            {
                segment = segment->Next();
                continue;
            }

            if( ++seg_count == 1 )  // if first connected item: then segment is candidate
            {
                candidate = segment;
                segment = segment->Next();
            }
            else        // More than 1 segment connected -> location is end of track
            {
                return;
            }
        }

        if( candidate )      // A candidate is found: flag it and push it in list
        {
            /* Initialize parameters to search items connected to this
             * candidate:
             * we must analyze connections to its other end
             */
            if( aPosition == candidate->GetStart() )
            {
                aPosition = candidate->GetEnd();
            }
            else
            {
                aPosition = candidate->GetStart();
            }

            layer_set = candidate->GetLayerSet();

            // flag this item and push it in list of selected items
            aList->push_back( candidate );
            candidate->SetState( BUSY, true );
        }
        else
        {
            return;
        }
    }
}


void BOARD::PushHighLight()
{
    m_highLightPrevious = m_highLight;
}


void BOARD::PopHighLight()
{
    m_highLight = m_highLightPrevious;
    m_highLightPrevious.Clear();
}


bool BOARD::SetLayerDescr( PCB_LAYER_ID aIndex, const LAYER& aLayer )
{
    if( unsigned( aIndex ) < DIM( m_Layer ) )
    {
        m_Layer[ aIndex ] = aLayer;
        return true;
    }

    return false;
}

#include <stdio.h>

const PCB_LAYER_ID BOARD::GetLayerID( const wxString& aLayerName ) const
{

    // Look for the BOARD specific copper layer names
    for( LAYER_NUM layer = 0; layer < PCB_LAYER_ID_COUNT; ++layer )
    {
        if ( IsCopperLayer( layer ) && ( m_Layer[ layer ].m_name == aLayerName ) )
        {
            return ToLAYER_ID( layer );
        }
    }

    // Otherwise fall back to the system standard layer names
    for( LAYER_NUM layer = 0; layer < PCB_LAYER_ID_COUNT; ++layer )
    {
        if( GetStandardLayerName( ToLAYER_ID( layer ) ) == aLayerName )
        {
            return ToLAYER_ID( layer );
        }
    }

    return UNDEFINED_LAYER;
}

const wxString BOARD::GetLayerName( PCB_LAYER_ID aLayer ) const
{
    // All layer names are stored in the BOARD.
    if( IsLayerEnabled( aLayer ) )
    {
        // Standard names were set in BOARD::BOARD() but they may be
        // over-ridden by BOARD::SetLayerName().
        // For copper layers, return the actual copper layer name,
        // otherwise return the Standard English layer name.
        if( IsCopperLayer( aLayer ) )
            return m_Layer[aLayer].m_name;
    }

    return GetStandardLayerName( aLayer );
}

bool BOARD::SetLayerName( PCB_LAYER_ID aLayer, const wxString& aLayerName )
{
    if( !IsCopperLayer( aLayer ) )
        return false;

    if( aLayerName == wxEmptyString || aLayerName.Len() > 20 )
        return false;

    // no quote chars in the name allowed
    if( aLayerName.Find( wxChar( '"' ) ) != wxNOT_FOUND )
        return false;

    wxString nameTemp = aLayerName;

    // replace any spaces with underscores before we do any comparing
    nameTemp.Replace( wxT( " " ), wxT( "_" ) );

    if( IsLayerEnabled( aLayer ) )
    {
#if 0
        for( LAYER_NUM i = FIRST_COPPER_LAYER; i < NB_COPPER_LAYERS; ++i )
        {
            if( i != aLayer && IsLayerEnabled( i ) && nameTemp == m_Layer[i].m_Name )
                return false;
        }
#else
        for( LSEQ cu = GetEnabledLayers().CuStack();  cu;  ++cu )
        {
            PCB_LAYER_ID id = *cu;

            // veto changing the name if it exists elsewhere.
            if( id != aLayer && nameTemp == m_Layer[id].m_name )
//            if( id != aLayer && nameTemp == wxString( m_Layer[id].m_name ) )
                return false;
        }
#endif

        m_Layer[aLayer].m_name = nameTemp;

        return true;
    }

    return false;
}


LAYER_T BOARD::GetLayerType( PCB_LAYER_ID aLayer ) const
{
    if( !IsCopperLayer( aLayer ) )
        return LT_SIGNAL;

    //@@IMB: The original test was broken due to the discontinuity
    // in the layer sequence.
    if( IsLayerEnabled( aLayer ) )
        return m_Layer[aLayer].m_type;

    return LT_SIGNAL;
}


bool BOARD::SetLayerType( PCB_LAYER_ID aLayer, LAYER_T aLayerType )
{
    if( !IsCopperLayer( aLayer ) )
        return false;

    //@@IMB: The original test was broken due to the discontinuity
    // in the layer sequence.
    if( IsLayerEnabled( aLayer ) )
    {
        m_Layer[aLayer].m_type = aLayerType;
        return true;
    }

    return false;
}


const char* LAYER::ShowType( LAYER_T aType )
{
    const char* cp;

    switch( aType )
    {
    default:
    case LT_SIGNAL:
        cp = "signal";
        break;

    case LT_POWER:
        cp = "power";
        break;

    case LT_MIXED:
        cp = "mixed";
        break;

    case LT_JUMPER:
        cp = "jumper";
        break;
    }

    return cp;
}


LAYER_T LAYER::ParseType( const char* aType )
{
    if( strcmp( aType, "signal" ) == 0 )
        return LT_SIGNAL;
    else if( strcmp( aType, "power" ) == 0 )
        return LT_POWER;
    else if( strcmp( aType, "mixed" ) == 0 )
        return LT_MIXED;
    else if( strcmp( aType, "jumper" ) == 0 )
        return LT_JUMPER;
    else
        return LT_UNDEFINED;
}


int BOARD::GetCopperLayerCount() const
{
    return m_designSettings.GetCopperLayerCount();
}


void BOARD::SetCopperLayerCount( int aCount )
{
    m_designSettings.SetCopperLayerCount( aCount );
}


LSET BOARD::GetEnabledLayers() const
{
    return m_designSettings.GetEnabledLayers();
}


LSET BOARD::GetVisibleLayers() const
{
    return m_designSettings.GetVisibleLayers();
}


void BOARD::SetEnabledLayers( LSET aLayerSet )
{
    m_designSettings.SetEnabledLayers( aLayerSet );
}


void BOARD::SetVisibleLayers( LSET aLayerSet )
{
    m_designSettings.SetVisibleLayers( aLayerSet );
}


void BOARD::SetVisibleElements( int aMask )
{
    // Call SetElementVisibility for each item
    // to ensure specific calculations that can be needed by some items,
    // just changing the visibility flags could be not sufficient.
    for( GAL_LAYER_ID ii = GAL_LAYER_ID_START; ii < GAL_LAYER_ID_BITMASK_END; ++ii )
    {
        int item_mask = 1 << GAL_LAYER_INDEX( ii );
        SetElementVisibility( ii, aMask & item_mask );
    }
}


void BOARD::SetVisibleAlls()
{
    SetVisibleLayers( LSET().set() );

    // Call SetElementVisibility for each item,
    // to ensure specific calculations that can be needed by some items
    for( GAL_LAYER_ID ii = GAL_LAYER_ID_START; ii < GAL_LAYER_ID_BITMASK_END; ++ii )
        SetElementVisibility( ii, true );
}


int BOARD::GetVisibleElements() const
{
    return m_designSettings.GetVisibleElements();
}


bool BOARD::IsElementVisible( GAL_LAYER_ID LAYER_aPCB ) const
{
    return m_designSettings.IsElementVisible( LAYER_aPCB );
}


void BOARD::SetElementVisibility( GAL_LAYER_ID LAYER_aPCB, bool isEnabled )
{
    m_designSettings.SetElementVisibility( LAYER_aPCB, isEnabled );

    switch( LAYER_aPCB )
    {
    case LAYER_RATSNEST:
    {
        bool visible = IsElementVisible( LAYER_RATSNEST );
        // we must clear or set the CH_VISIBLE flags to hide/show ratsnest
        // because we have a tool to show/hide ratsnest relative to a pad or a module
        // so the hide/show option is a per item selection

        for( unsigned int net = 1; net < GetNetCount(); net++ )
        {
            auto rn = GetConnectivity()->GetRatsnestForNet( net );
            if( rn )
                rn->SetVisible( visible );
        }

        for( auto track : Tracks() )
            track->SetLocalRatsnestVisible( isEnabled );

        for( auto mod : Modules() )
        {
            for( auto pad : mod->Pads() )
                pad->SetLocalRatsnestVisible( isEnabled );
        }

        for( int i = 0; i<GetAreaCount(); i++ )
        {
            auto zone = GetArea( i );
            zone->SetLocalRatsnestVisible( isEnabled );
        }

        m_Status_Pcb = 0;

        break;
    }

    default:
        ;
    }
}


COLOR4D BOARD::GetVisibleElementColor( GAL_LAYER_ID aLayerId )
{
    COLOR4D color = COLOR4D::UNSPECIFIED;

    switch( aLayerId )
    {
    case LAYER_NON_PLATED:
    case LAYER_VIA_THROUGH:
    case LAYER_VIA_MICROVIA:
    case LAYER_VIA_BBLIND:
    case LAYER_MOD_TEXT_FR:
    case LAYER_MOD_TEXT_BK:
    case LAYER_MOD_TEXT_INVISIBLE:
    case LAYER_ANCHOR:
    case LAYER_PAD_FR:
    case LAYER_PAD_BK:
    case LAYER_RATSNEST:
    case LAYER_GRID:
        color = GetColorsSettings()->GetItemColor( aLayerId );
        break;

    default:
        wxLogDebug( wxT( "BOARD::GetVisibleElementColor(): bad arg %d" ), aLayerId );
    }

    return color;
}


void BOARD::SetVisibleElementColor( GAL_LAYER_ID aLayerId, COLOR4D aColor )
{
    switch( aLayerId )
    {
    case LAYER_NON_PLATED:
    case LAYER_VIA_THROUGH:
    case LAYER_VIA_MICROVIA:
    case LAYER_VIA_BBLIND:
    case LAYER_MOD_TEXT_FR:
    case LAYER_MOD_TEXT_BK:
    case LAYER_MOD_TEXT_INVISIBLE:
    case LAYER_ANCHOR:
    case LAYER_PAD_FR:
    case LAYER_PAD_BK:
    case LAYER_GRID:
    case LAYER_RATSNEST:
        GetColorsSettings()->SetItemColor( aLayerId, aColor );
        break;

    default:
        wxLogDebug( wxT( "BOARD::SetVisibleElementColor(): bad arg %d" ), aLayerId );
    }
}


void BOARD::SetLayerColor( PCB_LAYER_ID aLayer, COLOR4D aColor )
{
    GetColorsSettings()->SetLayerColor( aLayer, aColor );
}


COLOR4D BOARD::GetLayerColor( PCB_LAYER_ID aLayer ) const
{
    return GetColorsSettings()->GetLayerColor( aLayer );
}


bool BOARD::IsModuleLayerVisible( PCB_LAYER_ID layer )
{
    switch( layer )
    {
    case F_Cu:
        return IsElementVisible( LAYER_MOD_FR );

    case B_Cu:
        return IsElementVisible( LAYER_MOD_BK );

    default:
        wxFAIL_MSG( wxT( "BOARD::IsModuleLayerVisible() param error: bad layer" ) );
        return true;
    }
}


void BOARD::Add( BOARD_ITEM* aBoardItem, ADD_MODE aMode )
{
    if( aBoardItem == NULL )
    {
        wxFAIL_MSG( wxT( "BOARD::Add() param error: aBoardItem NULL" ) );
        return;
    }

    switch( aBoardItem->Type() )
    {
    case PCB_NETINFO_T:
        m_NetInfo.AppendNet( (NETINFO_ITEM*) aBoardItem );
        break;

    // this one uses a vector
    case PCB_MARKER_T:
        m_markers.push_back( (MARKER_PCB*) aBoardItem );
        break;

    // this one uses a vector
    case PCB_ZONE_AREA_T:
        m_ZoneDescriptorList.push_back( (ZONE_CONTAINER*) aBoardItem );
        break;

    case PCB_TRACE_T:
    case PCB_VIA_T:
        if( aMode == ADD_APPEND )
        {
            m_Track.PushBack( (TRACK*) aBoardItem );
        }
        else
        {
            TRACK* insertAid;
            insertAid = ( (TRACK*) aBoardItem )->GetBestInsertPoint( this );
            m_Track.Insert( (TRACK*) aBoardItem, insertAid );
        }

        break;

    case PCB_ZONE_T:
        if( aMode == ADD_APPEND )
            m_Zone.PushBack( (SEGZONE*) aBoardItem );
        else
            m_Zone.PushFront( (SEGZONE*) aBoardItem );

        break;

    case PCB_MODULE_T:
        if( aMode == ADD_APPEND )
            m_Modules.PushBack( (MODULE*) aBoardItem );
        else
            m_Modules.PushFront( (MODULE*) aBoardItem );

        // Because the list of pads has changed, reset the status
        // This indicate the list of pad and nets must be recalculated before use
        m_Status_Pcb = 0;
        break;

    case PCB_DIMENSION_T:
    case PCB_LINE_T:
    case PCB_TEXT_T:
    case PCB_TARGET_T:
        if( aMode == ADD_APPEND )
            m_Drawings.PushBack( aBoardItem );
        else
            m_Drawings.PushFront( aBoardItem );

        break;

    // other types may use linked list
    default:
        {
            wxString msg;
            msg.Printf( wxT( "BOARD::Add() needs work: BOARD_ITEM type (%d) not handled" ),
                        aBoardItem->Type() );
            wxFAIL_MSG( msg );
            return;
        }
        break;
    }

    aBoardItem->SetParent( this );
    m_connectivity->Add( aBoardItem );
}


void BOARD::Remove( BOARD_ITEM* aBoardItem )
{
    // find these calls and fix them!  Don't send me no stinking' NULL.
    wxASSERT( aBoardItem );

    switch( aBoardItem->Type() )
    {
    case PCB_NETINFO_T:
    {
        NETINFO_ITEM* item = (NETINFO_ITEM*) aBoardItem;
        m_NetInfo.RemoveNet( item );
        break;
    }

    case PCB_MARKER_T:

        // find the item in the vector, then remove it
        for( unsigned i = 0; i<m_markers.size(); ++i )
        {
            if( m_markers[i] == (MARKER_PCB*) aBoardItem )
            {
                m_markers.erase( m_markers.begin() + i );
                break;
            }
        }

        break;

    case PCB_ZONE_AREA_T:    // this one uses a vector
        // find the item in the vector, then delete then erase it.
        for( unsigned i = 0; i<m_ZoneDescriptorList.size(); ++i )
        {
            if( m_ZoneDescriptorList[i] == (ZONE_CONTAINER*) aBoardItem )
            {
                m_ZoneDescriptorList.erase( m_ZoneDescriptorList.begin() + i );
                break;
            }
        }
        break;

    case PCB_MODULE_T:
        m_Modules.Remove( (MODULE*) aBoardItem );
        break;

    case PCB_TRACE_T:
    case PCB_VIA_T:
        m_Track.Remove( (TRACK*) aBoardItem );
        break;

    case PCB_ZONE_T:
        m_Zone.Remove( (SEGZONE*) aBoardItem );
        break;

    case PCB_DIMENSION_T:
    case PCB_LINE_T:
    case PCB_TEXT_T:
    case PCB_TARGET_T:
        m_Drawings.Remove( aBoardItem );
        break;

    // other types may use linked list
    default:
        wxFAIL_MSG( wxT( "BOARD::Remove() needs more ::Type() support" ) );
    }

    m_connectivity->Remove( aBoardItem );
}


void BOARD::DeleteMARKERs()
{
    // the vector does not know how to delete the MARKER_PCB, it holds pointers
    for( unsigned i = 0; i<m_markers.size(); ++i )
        delete m_markers[i];

    m_markers.clear();
}


void BOARD::DeleteZONEOutlines()
{
    // the vector does not know how to delete the ZONE Outlines, it holds
    // pointers
    for( unsigned i = 0; i<m_ZoneDescriptorList.size(); ++i )
        delete m_ZoneDescriptorList[i];

    m_ZoneDescriptorList.clear();
}


int BOARD::GetNumSegmTrack() const
{
    return m_Track.GetCount();
}


int BOARD::GetNumSegmZone() const
{
    return m_Zone.GetCount();
}


unsigned BOARD::GetNodesCount() const
{
    return m_connectivity->GetPadCount();
}


unsigned BOARD::GetUnconnectedNetCount() const
{
    return m_connectivity->GetUnconnectedCount();
}


EDA_RECT BOARD::ComputeBoundingBox( bool aBoardEdgesOnly ) const
{
    bool hasItems = false;
    EDA_RECT area;

    // Check segments, dimensions, texts, and fiducials
    for( BOARD_ITEM* item = m_Drawings;  item;  item = item->Next() )
    {
        if( aBoardEdgesOnly && (item->Type() != PCB_LINE_T || item->GetLayer() != Edge_Cuts ) )
            continue;

        if( !hasItems )
            area = item->GetBoundingBox();
        else
            area.Merge( item->GetBoundingBox() );

        hasItems = true;
    }

    if( !aBoardEdgesOnly )
    {
        // Check modules
        for( MODULE* module = m_Modules; module; module = module->Next() )
        {
            if( !hasItems )
                area = module->GetBoundingBox();
            else
                area.Merge( module->GetBoundingBox() );

            hasItems = true;
        }

        // Check tracks
        for( TRACK* track = m_Track; track; track = track->Next() )
        {
            if( !hasItems )
                area = track->GetBoundingBox();
            else
                area.Merge( track->GetBoundingBox() );

            hasItems = true;
        }

        // Check segment zones
        for( TRACK* track = m_Zone; track; track = track->Next() )
        {
            if( !hasItems )
                area = track->GetBoundingBox();
            else
                area.Merge( track->GetBoundingBox() );

            hasItems = true;
        }

        // Check polygonal zones
        for( unsigned int i = 0; i < m_ZoneDescriptorList.size(); i++ )
        {
            ZONE_CONTAINER* aZone = m_ZoneDescriptorList[i];

            if( !hasItems )
                area = aZone->GetBoundingBox();
            else
                area.Merge( aZone->GetBoundingBox() );

            area.Merge( aZone->GetBoundingBox() );
            hasItems = true;
        }
    }

    return area;
}


// virtual, see pcbstruct.h
void BOARD::GetMsgPanelInfo( std::vector< MSG_PANEL_ITEM >& aList )
{
    wxString txt;
    int      viasCount = 0;
    int      trackSegmentsCount = 0;

    for( BOARD_ITEM* item = m_Track; item; item = item->Next() )
    {
        if( item->Type() == PCB_VIA_T )
            viasCount++;
        else
            trackSegmentsCount++;
    }

    txt.Printf( wxT( "%d" ), GetPadCount() );
    aList.push_back( MSG_PANEL_ITEM( _( "Pads" ), txt, DARKGREEN ) );

    txt.Printf( wxT( "%d" ), viasCount );
    aList.push_back( MSG_PANEL_ITEM( _( "Vias" ), txt, DARKGREEN ) );

    txt.Printf( wxT( "%d" ), trackSegmentsCount );
    aList.push_back( MSG_PANEL_ITEM( _( "Track Segments" ), txt, DARKGREEN ) );

    txt.Printf( wxT( "%d" ), GetNodesCount() );
    aList.push_back( MSG_PANEL_ITEM( _( "Nodes" ), txt, DARKCYAN ) );

    txt.Printf( wxT( "%d" ), m_NetInfo.GetNetCount() );
    aList.push_back( MSG_PANEL_ITEM( _( "Nets" ), txt, RED ) );

    txt.Printf( wxT( "%d" ), GetConnectivity()->GetUnconnectedCount() );
    aList.push_back( MSG_PANEL_ITEM( _( "Unconnected" ), txt, BLUE ) );
}


// virtual, see pcbstruct.h
SEARCH_RESULT BOARD::Visit( INSPECTOR inspector, void* testData, const KICAD_T scanTypes[] )
{
    KICAD_T        stype;
    SEARCH_RESULT  result = SEARCH_CONTINUE;
    const KICAD_T* p    = scanTypes;
    bool           done = false;

#if 0 && defined(DEBUG)
    std::cout << GetClass().mb_str() << ' ';
#endif

    while( !done )
    {
        stype = *p;

        switch( stype )
        {
        case PCB_T:
            result = inspector( this, testData );  // inspect me
            // skip over any types handled in the above call.
            ++p;
            break;

        /*  Instances of the requested KICAD_T live in a list, either one
         *   that I manage, or that my modules manage.  If it's a type managed
         *   by class MODULE, then simply pass it on to each module's
         *   MODULE::Visit() function by way of the
         *   IterateForward( m_Modules, ... ) call.
         */

        case PCB_MODULE_T:
        case PCB_PAD_T:
        case PCB_MODULE_TEXT_T:
        case PCB_MODULE_EDGE_T:

            // this calls MODULE::Visit() on each module.
            result = IterateForward( m_Modules, inspector, testData, p );

            // skip over any types handled in the above call.
            for( ; ; )
            {
                switch( stype = *++p )
                {
                case PCB_MODULE_T:
                case PCB_PAD_T:
                case PCB_MODULE_TEXT_T:
                case PCB_MODULE_EDGE_T:
                    continue;

                default:
                    ;
                }

                break;
            }

            break;

        case PCB_LINE_T:
        case PCB_TEXT_T:
        case PCB_DIMENSION_T:
        case PCB_TARGET_T:
            result = IterateForward( m_Drawings, inspector, testData, p );

            // skip over any types handled in the above call.
            for( ; ; )
            {
                switch( stype = *++p )
                {
                case PCB_LINE_T:
                case PCB_TEXT_T:
                case PCB_DIMENSION_T:
                case PCB_TARGET_T:
                    continue;

                default:
                    ;
                }

                break;
            }

            ;
            break;

#if 0   // both these are on same list, so we must scan it twice in order
        // to get VIA priority, using new #else code below.
        // But we are not using separate lists for TRACKs and VIA, because
        // items are ordered (sorted) in the linked
        // list by netcode AND by physical distance:
        // when created, if a track or via is connected to an existing track or
        // via, it is put in linked list after this existing track or via
        // So usually, connected tracks or vias are grouped in this list
        // So the algorithm (used in ratsnest computations) which computes the
        // track connectivity is faster (more than 100 time regarding to
        // a non ordered list) because when it searches for a connection, first
        // it tests the near (near in term of linked list) 50 items
        // from the current item (track or via) in test.
        // Usually, because of this sort, a connected item (if exists) is
        // found.
        // If not found (and only in this case) an exhaustive (and time
        // consuming) search is made, but this case is statistically rare.
        case PCB_VIA_T:
        case PCB_TRACE_T:
            result = IterateForward( m_Track, inspector, testData, p );

            // skip over any types handled in the above call.
            for( ; ; )
            {
                switch( stype = *++p )
                {
                case PCB_VIA_T:
                case PCB_TRACE_T:
                    continue;

                default:
                    ;
                }

                break;
            }

            break;

#else
        case PCB_VIA_T:
            result = IterateForward( m_Track, inspector, testData, p );
            ++p;
            break;

        case PCB_TRACE_T:
            result = IterateForward( m_Track, inspector, testData, p );
            ++p;
            break;
#endif

        case PCB_MARKER_T:

            // MARKER_PCBS are in the m_markers std::vector
            for( unsigned i = 0; i<m_markers.size(); ++i )
            {
                result = m_markers[i]->Visit( inspector, testData, p );

                if( result == SEARCH_QUIT )
                    break;
            }

            ++p;
            break;

        case PCB_ZONE_AREA_T:

            // PCB_ZONE_AREA_T are in the m_ZoneDescriptorList std::vector
            for( unsigned i = 0; i< m_ZoneDescriptorList.size(); ++i )
            {
                result = m_ZoneDescriptorList[i]->Visit( inspector, testData, p );

                if( result == SEARCH_QUIT )
                    break;
            }

            ++p;
            break;

        case PCB_ZONE_T:
            result = IterateForward( m_Zone, inspector, testData, p );
            ++p;
            break;

        default:        // catch EOT or ANY OTHER type here and return.
            done = true;
            break;
        }

        if( result == SEARCH_QUIT )
            break;
    }

    return result;
}


NETINFO_ITEM* BOARD::FindNet( int aNetcode ) const
{
    // the first valid netcode is 1 and the last is m_NetInfo.GetCount()-1.
    // zero is reserved for "no connection" and is not actually a net.
    // NULL is returned for non valid netcodes

    wxASSERT( m_NetInfo.GetNetCount() > 0 );    // net zero should exist

    if( aNetcode == NETINFO_LIST::UNCONNECTED && m_NetInfo.GetNetCount() == 0 )
        return &NETINFO_LIST::ORPHANED_ITEM;
    else
        return m_NetInfo.GetNetItem( aNetcode );
}


NETINFO_ITEM* BOARD::FindNet( const wxString& aNetname ) const
{
    return m_NetInfo.GetNetItem( aNetname );
}


MODULE* BOARD::FindModuleByReference( const wxString& aReference ) const
{
    MODULE* found = nullptr;

    // search only for MODULES
    static const KICAD_T scanTypes[] = { PCB_MODULE_T, EOT };

    INSPECTOR_FUNC inspector = [&] ( EDA_ITEM* item, void* testData )
    {
        MODULE* module = (MODULE*) item;

        if( aReference == module->GetReference() )
        {
            found = module;
            return SEARCH_QUIT;
        }

        return SEARCH_CONTINUE;
    };

    // visit this BOARD with the above inspector
    BOARD* nonconstMe = (BOARD*) this;
    nonconstMe->Visit( inspector, NULL, scanTypes );

    return found;
}


MODULE* BOARD::FindModule( const wxString& aRefOrTimeStamp, bool aSearchByTimeStamp ) const
{
    if( aSearchByTimeStamp )
    {
        for( MODULE* module = m_Modules;  module;  module = module->Next() )
        {
            if( aRefOrTimeStamp.CmpNoCase( module->GetPath() ) == 0 )
                return module;
        }
    }
    else
    {
        return FindModuleByReference( aRefOrTimeStamp );
    }

    return NULL;
}


// Sort nets by decreasing pad count. For same pad count, sort by alphabetic names
static bool sortNetsByNodes( const NETINFO_ITEM* a, const NETINFO_ITEM* b )
{
    auto connectivity = a->GetParent()->GetConnectivity();
    int countA = connectivity->GetPadCount( a->GetNet() );
    int countB = connectivity->GetPadCount( b->GetNet() );

    if( countA == countB )
        return a->GetNetname() < b->GetNetname();
    else
        return countB < countA;
}

// Sort nets by alphabetic names
static bool sortNetsByNames( const NETINFO_ITEM* a, const NETINFO_ITEM* b )
{
    return a->GetNetname() < b->GetNetname();
}

int BOARD::SortedNetnamesList( wxArrayString& aNames, bool aSortbyPadsCount )
{
    if( m_NetInfo.GetNetCount() == 0 )
        return 0;

    // Build the list
    std::vector <NETINFO_ITEM*> netBuffer;

    netBuffer.reserve( m_NetInfo.GetNetCount() );

    for( NETINFO_LIST::iterator net( m_NetInfo.begin() ), netEnd( m_NetInfo.end() );
                net != netEnd; ++net )
    {
        if( net->GetNet() > 0 )
            netBuffer.push_back( *net );
    }

    // sort the list
    if( aSortbyPadsCount )
        sort( netBuffer.begin(), netBuffer.end(), sortNetsByNodes );
    else
        sort( netBuffer.begin(), netBuffer.end(), sortNetsByNames );

    for( unsigned ii = 0; ii <  netBuffer.size(); ii++ )
        aNames.Add( netBuffer[ii]->GetNetname() );

    return netBuffer.size();
}


void BOARD::RedrawAreasOutlines( EDA_DRAW_PANEL* panel, wxDC* aDC, GR_DRAWMODE aDrawMode, PCB_LAYER_ID aLayer )
{
    if( !aDC )
        return;

    for( int ii = 0; ii < GetAreaCount(); ii++ )
    {
        ZONE_CONTAINER* edge_zone = GetArea( ii );

        if( (aLayer < 0) || ( aLayer == edge_zone->GetLayer() ) )
            edge_zone->Draw( panel, aDC, aDrawMode );
    }
}


void BOARD::RedrawFilledAreas( EDA_DRAW_PANEL* panel, wxDC* aDC, GR_DRAWMODE aDrawMode, PCB_LAYER_ID aLayer )
{
    if( !aDC )
        return;

    for( int ii = 0; ii < GetAreaCount(); ii++ )
    {
        ZONE_CONTAINER* edge_zone = GetArea( ii );

        if( (aLayer < 0) || ( aLayer == edge_zone->GetLayer() ) )
            edge_zone->DrawFilledArea( panel, aDC, aDrawMode );
    }
}


ZONE_CONTAINER* BOARD::HitTestForAnyFilledArea( const wxPoint& aRefPos,
    PCB_LAYER_ID aStartLayer, PCB_LAYER_ID aEndLayer,  int aNetCode )
{
    if( aEndLayer < 0 )
        aEndLayer = aStartLayer;

    if( aEndLayer <  aStartLayer )
        std::swap( aEndLayer, aStartLayer );

    for( unsigned ia = 0; ia < m_ZoneDescriptorList.size(); ia++ )
    {
        ZONE_CONTAINER* area  = m_ZoneDescriptorList[ia];
        LAYER_NUM       layer = area->GetLayer();

        if( layer < aStartLayer || layer > aEndLayer )
            continue;

        // In locate functions we must skip tagged items with BUSY flag set.
        if( area->GetState( BUSY ) )
            continue;

        if( aNetCode >= 0 && area->GetNetCode() != aNetCode )
            continue;

        if( area->HitTestFilledArea( aRefPos ) )
            return area;
    }

    return NULL;
}


int BOARD::SetAreasNetCodesFromNetNames()
{
    int error_count = 0;

    for( int ii = 0; ii < GetAreaCount(); ii++ )
    {
        ZONE_CONTAINER* it = GetArea( ii );

        if( !it->IsOnCopperLayer() )
        {
            it->SetNetCode( NETINFO_LIST::UNCONNECTED );
            continue;
        }

        if( it->GetNetCode() != 0 )      // i.e. if this zone is connected to a net
        {
            const NETINFO_ITEM* net = it->GetNet();

            if( net )
            {
                it->SetNetCode( net->GetNet() );
            }
            else
            {
                error_count++;

                // keep Net Name and set m_NetCode to -1 : error flag.
                it->SetNetCode( -1 );
            }
        }
    }

    return error_count;
}


VIA* BOARD::GetViaByPosition( const wxPoint& aPosition, PCB_LAYER_ID aLayer) const
{
    for( VIA *via = GetFirstVia( m_Track); via; via = GetFirstVia( via->Next() ) )
    {
        if( (via->GetStart() == aPosition) &&
                (via->GetState( BUSY | IS_DELETED ) == 0) &&
                ((aLayer == UNDEFINED_LAYER) || (via->IsOnLayer( aLayer ))) )
            return via;
    }

    return NULL;
}


D_PAD* BOARD::GetPad( const wxPoint& aPosition, LSET aLayerSet )
{
    if( !aLayerSet.any() )
        aLayerSet = LSET::AllCuMask();

    for( MODULE* module = m_Modules;  module;  module = module->Next() )
    {
        D_PAD* pad = module->GetPad( aPosition, aLayerSet );

        if( pad )
            return pad;
    }

    return NULL;
}


D_PAD* BOARD::GetPad( TRACK* aTrace, ENDPOINT_T aEndPoint )
{
    const wxPoint& aPosition = aTrace->GetEndPoint( aEndPoint );

    LSET lset( aTrace->GetLayer() );

    for( MODULE* module = m_Modules;  module;  module = module->Next() )
    {
        D_PAD*  pad = module->GetPad( aPosition, lset );

        if( pad )
            return pad;
    }

    return NULL;
}


D_PAD* BOARD::GetPadFast( const wxPoint& aPosition, LSET aLayerSet )
{
    for( auto mod : Modules() )
    {
        for ( auto pad : mod->Pads() )
        {
        if( pad->GetPosition() != aPosition )
            continue;

        // Pad found, it must be on the correct layer
        if( ( pad->GetLayerSet() & aLayerSet ).any() )
            return pad;
    }
}

    return nullptr;
}


D_PAD* BOARD::GetPad( std::vector<D_PAD*>& aPadList, const wxPoint& aPosition, LSET aLayerSet )
{
    // Search aPadList for aPosition
    // aPadList is sorted by X then Y values, and a fast binary search is used
    int idxmax = aPadList.size()-1;

    int delta = aPadList.size();

    int idx = 0;        // Starting index is the beginning of list

    while( delta )
    {
        // Calculate half size of remaining interval to test.
        // Ensure the computed value is not truncated (too small)
        if( (delta & 1) && ( delta > 1 ) )
            delta++;

        delta /= 2;

        D_PAD* pad = aPadList[idx];

        if( pad->GetPosition() == aPosition )       // candidate found
        {
            // The pad must match the layer mask:
            if( ( aLayerSet & pad->GetLayerSet() ).any() )
                return pad;

            // More than one pad can be at aPosition
            // search for a pad at aPosition that matched this mask

            // search next
            for( int ii = idx+1; ii <= idxmax; ii++ )
            {
                pad = aPadList[ii];

                if( pad->GetPosition() != aPosition )
                    break;

                if( ( aLayerSet & pad->GetLayerSet() ).any() )
                    return pad;
            }
            // search previous
            for(  int ii = idx-1 ;ii >=0; ii-- )
            {
                pad = aPadList[ii];

                if( pad->GetPosition() != aPosition )
                    break;

                if( ( aLayerSet & pad->GetLayerSet() ).any() )
                    return pad;
            }

            // Not found:
            return 0;
        }

        if( pad->GetPosition().x == aPosition.x )       // Must search considering Y coordinate
        {
            if( pad->GetPosition().y < aPosition.y )    // Must search after this item
            {
                idx += delta;

                if( idx > idxmax )
                    idx = idxmax;
            }
            else // Must search before this item
            {
                idx -= delta;

                if( idx < 0 )
                    idx = 0;
            }
        }
        else if( pad->GetPosition().x < aPosition.x ) // Must search after this item
        {
            idx += delta;

            if( idx > idxmax )
                idx = idxmax;
        }
        else // Must search before this item
        {
            idx -= delta;

            if( idx < 0 )
                idx = 0;
        }
    }

    return NULL;
}


/**
 * Function SortPadsByXCoord
 * is used by GetSortedPadListByXCoord to Sort a pad list by x coordinate value.
 * This function is used to build ordered pads lists
 */
bool sortPadsByXthenYCoord( D_PAD* const & ref, D_PAD* const & comp )
{
    if( ref->GetPosition().x == comp->GetPosition().x )
        return ref->GetPosition().y < comp->GetPosition().y;
    return ref->GetPosition().x < comp->GetPosition().x;
}


void BOARD::GetSortedPadListByXthenYCoord( std::vector<D_PAD*>& aVector, int aNetCode )
{
    for ( auto mod : Modules() )
    {
        for ( auto pad : mod->Pads( ) )
        {
            if( aNetCode < 0 ||  pad->GetNetCode() == aNetCode )
            {
                aVector.push_back( pad );
            }
        }
    }

    std::sort( aVector.begin(), aVector.end(), sortPadsByXthenYCoord );
}


void BOARD::PadDelete( D_PAD* aPad )
{
    aPad->DeleteStructure();
}


TRACK* BOARD::GetVisibleTrack( TRACK* aStartingTrace, const wxPoint& aPosition,
        LSET aLayerSet ) const
{
    for( TRACK* track = aStartingTrace; track; track = track->Next() )
    {
        PCB_LAYER_ID layer = track->GetLayer();

        if( track->GetState( BUSY | IS_DELETED ) )
            continue;

        // track's layer is not visible
        if( m_designSettings.IsLayerVisible( layer ) == false )
            continue;

        if( track->Type() == PCB_VIA_T )    // VIA encountered.
        {
            if( track->HitTest( aPosition ) )
                return track;
        }
        else
        {
            if( !aLayerSet[layer] )
                continue;               // track's layer is not in aLayerSet

            if( track->HitTest( aPosition ) )
                return track;
        }
    }

    return NULL;
}


#if defined(DEBUG) && 0
static void dump_tracks( const char* aName, const TRACKS& aList )
{
    printf( "%s: count=%zd\n", aName, aList.size() );

    for( unsigned i = 0; i < aList.size();  ++i )
    {
        TRACK*  seg = aList[i];
        ::VIA*  via = dynamic_cast< ::VIA* >( seg );

        if( via )
            printf( " via[%u]: (%d, %d)\n", i, via->GetStart().x, via->GetStart().y );
        else
            printf( " seg[%u]: (%d, %d) (%d, %d)\n", i,
                    seg->GetStart().x, seg->GetStart().y,
                    seg->GetEnd().x,   seg->GetEnd().y );
    }
}
#endif




TRACK* BOARD::MarkTrace( TRACK*  aTrace, int* aCount,
                         double* aTraceLength, double* aPadToDieLength,
                         bool    aReorder )
{
    TRACKS trackList;

    if( aCount )
        *aCount = 0;

    if( aTraceLength )
        *aTraceLength = 0;

    if( aTrace == NULL )
        return NULL;

    // Ensure the flag BUSY of all tracks of the board is cleared
    // because we use it to mark segments of the track
    for( TRACK* track = m_Track; track; track = track->Next() )
        track->SetState( BUSY, false );

    // Set flags of the initial track segment
    aTrace->SetState( BUSY, true );
    LSET layer_set = aTrace->GetLayerSet();

    trackList.push_back( aTrace );

    /* Examine the initial track segment : if it is really a segment, this is
     * easy.
     *  If it is a via, one must search for connected segments.
     *  If <=2, this via connect 2 segments (or is connected to only one
     *  segment) and this via and these 2 segments are a part of a track.
     *  If > 2 only this via is flagged (the track has only this via)
     */
    if( aTrace->Type() == PCB_VIA_T )
    {
        TRACK* segm1 = ::GetTrack( m_Track, NULL, aTrace->GetStart(), layer_set );
        TRACK* segm2 = NULL;
        TRACK* segm3 = NULL;

        if( segm1 )
        {
            segm2 = ::GetTrack( segm1->Next(), NULL, aTrace->GetStart(), layer_set );
        }

        if( segm2 )
        {
            segm3 = ::GetTrack( segm2->Next(), NULL, aTrace->GetStart(), layer_set );
        }

        if( segm3 )
        {
            // More than 2 segments are connected to this via.
            // The "track" is only this via.

            if( aCount )
                *aCount = 1;

            return aTrace;
        }

        if( segm1 ) // search for other segments connected to the initial segment start point
        {
            layer_set = segm1->GetLayerSet();
            chainMarkedSegments( aTrace->GetStart(), layer_set, &trackList );
        }

        if( segm2 ) // search for other segments connected to the initial segment end point
        {
            layer_set = segm2->GetLayerSet();
            chainMarkedSegments( aTrace->GetStart(), layer_set, &trackList );
        }
    }
    else    // mark the chain using both ends of the initial segment
    {
        TRACKS  from_start;
        TRACKS  from_end;

        chainMarkedSegments( aTrace->GetStart(), layer_set, &from_start );
        chainMarkedSegments( aTrace->GetEnd(),   layer_set, &from_end );

        // DBG( dump_tracks( "first_clicked", trackList ); )
        // DBG( dump_tracks( "from_start", from_start ); )
        // DBG( dump_tracks( "from_end",   from_end ); )

        // combine into one trackList:
        trackList.insert( trackList.end(), from_start.begin(), from_start.end() );
        trackList.insert( trackList.end(), from_end.begin(),   from_end.end() );
    }

    // Now examine selected vias and flag them if they are on the track
    // If a via is connected to only one or 2 segments, it is flagged (is on the track)
    // If a via is connected to more than 2 segments, it is a track end, and it
    // is removed from the list.
    // Go through the list backwards.
    for( int i = trackList.size() - 1;  i>=0;  --i )
    {
        ::VIA*  via = dynamic_cast< ::VIA* >( trackList[i] );

        if( !via )
            continue;

        if( via == aTrace )
            continue;

        via->SetState( BUSY, true );  // Try to flag it. the flag will be cleared later if needed

        layer_set = via->GetLayerSet();

        TRACK* track = ::GetTrack( m_Track, NULL, via->GetStart(), layer_set );

        // GetTrace does not consider tracks flagged BUSY.
        // So if no connected track found, this via is on the current track
        // only: keep it
        if( track == NULL )
            continue;

        /* If a track is found, this via connects also other segments of
         * another track.  This case happens when a via ends the selected
         * track but must we consider this via is on the selected track, or
         * on another track.
         * (this is important when selecting a track for deletion: must this
         * via be deleted or not?)
         * We consider this via to be on our track if other segments connected
         * to this via remain connected when removing this via.
         * We search for all other segments connected together:
         * if they are on the same layer, then the via is on the selected track;
         * if they are on different layers, the via is on another track.
         */
        LAYER_NUM layer = track->GetLayer();

        while( ( track = ::GetTrack( track->Next(), NULL, via->GetStart(), layer_set ) ) != NULL )
        {
            if( layer != track->GetLayer() )
            {
                // The via connects segments of another track: it is removed
                // from list because it is member of another track

                DBG(printf( "%s: omit track (%d, %d) (%d, %d) on layer:%d (!= our_layer:%d)\n",
                    __func__,
                    track->GetStart().x, track->GetStart().y,
                    track->GetEnd().x, track->GetEnd().y,
                    track->GetLayer(), layer
                    ); )

                via->SetState( BUSY, false );
                break;
            }
        }
    }

    /* Rearrange the track list in order to have flagged segments linked
     * from firstTrack so the NbSegmBusy segments are consecutive segments
     * in list, the first item in the full track list is firstTrack, and
     * the NbSegmBusy-1 next items (NbSegmBusy when including firstTrack)
     * are the flagged segments
     */
    int     busy_count = 0;
    TRACK*  firstTrack;

    for( firstTrack = m_Track; firstTrack; firstTrack = firstTrack->Next() )
    {
        // Search for the first flagged BUSY segments
        if( firstTrack->GetState( BUSY ) )
        {
            busy_count = 1;
            break;
        }
    }

    if( firstTrack == NULL )
        return NULL;

    // First step: calculate the track length and find the pads (when exist)
    // at each end of the trace.
    double full_len = 0;
    double lenPadToDie = 0;
    // Because we have a track (a set of track segments between 2 nodes),
    // only 2 pads (maximum) will be taken in account:
    // that are on each end of the track, if any.
    // keep trace of them, to know the die length and the track length ibside each pad.
    D_PAD* s_pad = NULL;        // the pad on one end of the trace
    D_PAD* e_pad = NULL;        // the pad on the other end of the trace
    int dist_fromstart = INT_MAX;
    int dist_fromend = INT_MAX;

    for( TRACK* track = firstTrack; track; track = track->Next() )
    {
        if( !track->GetState( BUSY ) )
            continue;

        layer_set = track->GetLayerSet();
        D_PAD * pad_on_start = GetPad( track->GetStart(), layer_set );
        D_PAD * pad_on_end = GetPad( track->GetEnd(), layer_set );

        // a segment fully inside a pad does not contribute to the track len
        // (an other track end inside this pad will contribute to this lenght)
        if( pad_on_start && ( pad_on_start == pad_on_end ) )
            continue;

        full_len += track->GetLength();

        if( pad_on_start == NULL && pad_on_end == NULL )
            // This most of time the case
            continue;

        // At this point, we can have one track end on a pad, or the 2 track ends on
        // 2 different pads.
        // We don't know what pad (s_pad or e_pad) must be used to store the
        // start point and the end point of the track, so if a pad is already set,
        // use the other
        if( pad_on_start )
        {
            SEG segm( track->GetStart(), pad_on_start->GetPosition() );
            int dist = segm.Length();

            if( s_pad == NULL )
            {
                dist_fromstart = dist;
                s_pad = pad_on_start;
            }
            else if( e_pad == NULL )
            {
                dist_fromend = dist;
                e_pad = pad_on_start;
            }
            else    // Should not occur, at least for basic pads
            {
                // wxLogMessage( "BOARD::MarkTrace: multiple pad_on_start" );
            }
        }

        if( pad_on_end )
        {
            SEG segm( track->GetEnd(), pad_on_end->GetPosition() );
            int dist = segm.Length();

            if( s_pad == NULL )
            {
                dist_fromstart = dist;
                s_pad = pad_on_end;
            }
            else if( e_pad == NULL )
            {
                dist_fromend = dist;
                e_pad = pad_on_end;
            }
            else    // Should not occur, at least for basic pads
            {
                // wxLogMessage( "BOARD::MarkTrace: multiple pad_on_end" );
            }
        }
    }

    if( aReorder )
    {
        DLIST<TRACK>* list = (DLIST<TRACK>*)firstTrack->GetList();
        wxASSERT( list );

        /* Rearrange the chain starting at firstTrack
         * All other BUSY flagged items are moved from their position to the end
         * of the flagged list
         */
        TRACK* next;

        for( TRACK* track = firstTrack->Next(); track; track = next )
        {
            next = track->Next();

            if( track->GetState( BUSY ) )   // move it!
            {
                busy_count++;
                track->UnLink();
                list->Insert( track, firstTrack->Next() );

            }
        }
    }
    else if( aTraceLength )
    {
        busy_count = 0;

        for( TRACK* track = firstTrack; track; track = track->Next() )
        {
            if( track->GetState( BUSY ) )
            {
                busy_count++;
                track->SetState( BUSY, false );
            }
        }

        DBG( printf( "%s: busy_count:%d\n", __func__, busy_count ); )
    }

    if( s_pad )
    {
        full_len += dist_fromstart;
        lenPadToDie += (double) s_pad->GetPadToDieLength();
    }

    if( e_pad )
    {
        full_len += dist_fromend;
        lenPadToDie += (double) e_pad->GetPadToDieLength();
    }

    if( aTraceLength )
        *aTraceLength = full_len;

    if( aPadToDieLength )
        *aPadToDieLength = lenPadToDie;

    if( aCount )
        *aCount = busy_count;

    return firstTrack;
}


MODULE* BOARD::GetFootprint( const wxPoint& aPosition, PCB_LAYER_ID aActiveLayer,
                             bool aVisibleOnly, bool aIgnoreLocked )
{
    MODULE* pt_module;
    MODULE* module      = NULL;
    MODULE* alt_module  = NULL;
    int     min_dim     = 0x7FFFFFFF;
    int     alt_min_dim = 0x7FFFFFFF;
    bool    current_layer_back = IsBackLayer( aActiveLayer );

    for( pt_module = m_Modules;  pt_module;  pt_module = pt_module->Next() )
    {
        // is the ref point within the module's bounds?
        if( !pt_module->HitTest( aPosition ) )
            continue;

        // if caller wants to ignore locked modules, and this one is locked, skip it.
        if( aIgnoreLocked && pt_module->IsLocked() )
            continue;

        PCB_LAYER_ID layer = pt_module->GetLayer();

        // Filter non visible modules if requested
        if( !aVisibleOnly || IsModuleLayerVisible( layer ) )
        {
            EDA_RECT bb = pt_module->GetFootprintRect();

            int offx = bb.GetX() + bb.GetWidth() / 2;
            int offy = bb.GetY() + bb.GetHeight() / 2;

            // off x & offy point to the middle of the box.
            int dist = ( aPosition.x - offx ) * ( aPosition.x - offx ) +
                       ( aPosition.y - offy ) * ( aPosition.y - offy );

            if( current_layer_back == IsBackLayer( layer ) )
            {
                if( dist <= min_dim )
                {
                    // better footprint shown on the active side
                    module  = pt_module;
                    min_dim = dist;
                }
            }
            else if( aVisibleOnly && IsModuleLayerVisible( layer ) )
            {
                if( dist <= alt_min_dim )
                {
                    // better footprint shown on the other side
                    alt_module  = pt_module;
                    alt_min_dim = dist;
                }
            }
        }
    }

    if( module )
    {
        return module;
    }

    if( alt_module)
    {
        return alt_module;
    }

    return NULL;
}


BOARD_CONNECTED_ITEM* BOARD::GetLockPoint( const wxPoint& aPosition, LSET aLayerSet )
{
    for( MODULE* module = m_Modules; module; module = module->Next() )
    {
        D_PAD* pad = module->GetPad( aPosition, aLayerSet );

        if( pad )
            return pad;
    }

    // No pad has been located so check for a segment of the trace.
    TRACK* segment = ::GetTrack( m_Track, NULL, aPosition, aLayerSet );

    if( !segment )
        segment = GetVisibleTrack( m_Track, aPosition, aLayerSet );

    return segment;
}


TRACK* BOARD::CreateLockPoint( wxPoint& aPosition, TRACK* aSegment, PICKED_ITEMS_LIST* aList )
{
    /* creates an intermediate point on aSegment and break it into two segments
     * at aPosition.
     * The new segment starts from aPosition and ends at the end point of
     * aSegment. The original segment now ends at aPosition.
     */
    if( aSegment->GetStart() == aPosition || aSegment->GetEnd() == aPosition )
        return NULL;

    // A via is a good lock point
    if( aSegment->Type() == PCB_VIA_T )
    {
        aPosition = aSegment->GetStart();
        return aSegment;
    }

    // Calculation coordinate of intermediate point relative to the start point of aSegment
     wxPoint delta = aSegment->GetEnd() - aSegment->GetStart();

    // calculate coordinates of aPosition relative to aSegment->GetStart()
    wxPoint lockPoint = aPosition - aSegment->GetStart();

    // lockPoint must be on aSegment:
    // Ensure lockPoint.y/lockPoint.y = delta.y/delta.x
    if( delta.x == 0 )
        lockPoint.x = 0;         // horizontal segment
    else
        lockPoint.y = KiROUND( ( (double)lockPoint.x * delta.y ) / delta.x );

    /* Create the intermediate point (that is to say creation of a new
     * segment, beginning at the intermediate point.
     */
    lockPoint += aSegment->GetStart();

    TRACK* newTrack = (TRACK*)aSegment->Clone();
    // The new segment begins at the new point,
    newTrack->SetStart(lockPoint);
    newTrack->start = aSegment;
    newTrack->SetState( BEGIN_ONPAD, false );

    DLIST<TRACK>* list = (DLIST<TRACK>*)aSegment->GetList();
    wxASSERT( list );
    list->Insert( newTrack, aSegment->Next() );

    if( aList )
    {
        // Prepare the undo command for the now track segment
        ITEM_PICKER picker( newTrack, UR_NEW );
        aList->PushItem( picker );
        // Prepare the undo command for the old track segment
        // before modifications
        picker.SetItem( aSegment );
        picker.SetStatus( UR_CHANGED );
        picker.SetLink( aSegment->Clone() );
        aList->PushItem( picker );
    }

    // Old track segment now ends at new point.
    aSegment->SetEnd(lockPoint);
    aSegment->end = newTrack;
    aSegment->SetState( END_ONPAD, false );

    D_PAD * pad = GetPad( newTrack, ENDPOINT_START );

    if( pad )
    {
        newTrack->start = pad;
        newTrack->SetState( BEGIN_ONPAD, true );
        aSegment->end = pad;
        aSegment->SetState( END_ONPAD, true );
    }

    aPosition = lockPoint;
    return newTrack;
}


ZONE_CONTAINER* BOARD::AddArea( PICKED_ITEMS_LIST* aNewZonesList, int aNetcode,
                                PCB_LAYER_ID aLayer, wxPoint aStartPointPosition, int aHatch )
{
    ZONE_CONTAINER* new_area = InsertArea( aNetcode,
                                           m_ZoneDescriptorList.size( ) - 1,
                                           aLayer, aStartPointPosition.x,
                                           aStartPointPosition.y, aHatch );

    if( aNewZonesList )
    {
        ITEM_PICKER picker( new_area, UR_NEW );
        aNewZonesList->PushItem( picker );
    }

    return new_area;
}


void BOARD::RemoveArea( PICKED_ITEMS_LIST* aDeletedList, ZONE_CONTAINER* area_to_remove )
{
    if( area_to_remove == NULL )
        return;

    if( aDeletedList )
    {
        ITEM_PICKER picker( area_to_remove, UR_DELETED );
        aDeletedList->PushItem( picker );
        Remove( area_to_remove );   // remove from zone list, but does not delete it
    }
    else
    {
        Delete( area_to_remove );
    }
}


ZONE_CONTAINER* BOARD::InsertArea( int aNetcode, int aAreaIdx, PCB_LAYER_ID aLayer,
                                   int aCornerX, int aCornerY, int aHatch )
{
    ZONE_CONTAINER* new_area = new ZONE_CONTAINER( this );

    new_area->SetNetCode( aNetcode );
    new_area->SetLayer( aLayer );
    new_area->SetTimeStamp( GetNewTimeStamp() );

    if( aAreaIdx < (int) ( m_ZoneDescriptorList.size() - 1 ) )
        m_ZoneDescriptorList.insert( m_ZoneDescriptorList.begin() + aAreaIdx + 1, new_area );
    else
        m_ZoneDescriptorList.push_back( new_area );

    new_area->SetHatchStyle( (ZONE_CONTAINER::HATCH_STYLE) aHatch );

    // Add the first corner to the new zone
    new_area->AppendCorner( wxPoint( aCornerX, aCornerY ), -1 );

    return new_area;
}


bool BOARD::NormalizeAreaPolygon( PICKED_ITEMS_LIST * aNewZonesList, ZONE_CONTAINER* aCurrArea )
{
    // mark all areas as unmodified except this one, if modified
    for( unsigned ia = 0; ia < m_ZoneDescriptorList.size(); ia++ )
        m_ZoneDescriptorList[ia]->SetLocalFlags( 0 );

    aCurrArea->SetLocalFlags( 1 );

    if( aCurrArea->Outline()->IsSelfIntersecting() )
    {
        aCurrArea->UnHatch();

        // Normalize copied area and store resulting number of polygons
        int n_poly = aCurrArea->Outline()->NormalizeAreaOutlines();

        // If clipping has created some polygons, we must add these new copper areas.
        if( n_poly > 1 )
        {
            ZONE_CONTAINER* NewArea;

            // Move the newly created polygons to new areas, removing them from the current area
            for( int ip = 1; ip < n_poly; ip++ )
            {
                // Create new copper area and copy poly into it
                SHAPE_POLY_SET* new_p = new SHAPE_POLY_SET( aCurrArea->Outline()->UnitSet( ip ) );
                NewArea = AddArea( aNewZonesList, aCurrArea->GetNetCode(), aCurrArea->GetLayer(),
                                   wxPoint(0, 0), aCurrArea->GetHatchStyle() );

                // remove the poly that was automatically created for the new area
                // and replace it with a poly from NormalizeAreaOutlines
                delete NewArea->Outline();
                NewArea->SetOutline( new_p );
                NewArea->Hatch();
                NewArea->SetLocalFlags( 1 );
            }

            SHAPE_POLY_SET* new_p = new SHAPE_POLY_SET( aCurrArea->Outline()->UnitSet( 0 ) );
            delete aCurrArea->Outline();
            aCurrArea->SetOutline( new_p );
        }
    }

    aCurrArea->Hatch();

    return true;
}


void BOARD::ReplaceNetlist( NETLIST& aNetlist, bool aDeleteSinglePadNets,
                            std::vector<MODULE*>* aNewFootprints, REPORTER* aReporter )
{
    unsigned       i;
    wxPoint        bestPosition;
    wxString       msg;
    std::vector<MODULE*> newFootprints;

    if( !IsEmpty() )
    {
        // Position new components below any existing board features.
        EDA_RECT bbbox = GetBoardEdgesBoundingBox();

        if( bbbox.GetWidth() || bbbox.GetHeight() )
        {
            bestPosition.x = bbbox.Centre().x;
            bestPosition.y = bbbox.GetBottom() + Millimeter2iu( 10 );
        }
    }
    else
    {
        // Position new components in the center of the page when the board is empty.
        wxSize pageSize = m_paper.GetSizeIU();

        bestPosition.x = pageSize.GetWidth() / 2;
        bestPosition.y = pageSize.GetHeight() / 2;
    }

    m_Status_Pcb = 0;

    for( i = 0;  i < aNetlist.GetCount();  i++ )
    {
        COMPONENT* component = aNetlist.GetComponent( i );
        MODULE* footprint;

        if( aReporter )
        {

            msg.Printf( _( "Checking netlist component footprint \"%s:%s:%s\".\n" ),
                        GetChars( component->GetReference() ),
                        GetChars( component->GetTimeStamp() ),
                        GetChars( FROM_UTF8( component->GetFPID().Format() ) ) );
            aReporter->Report( msg, REPORTER::RPT_INFO );
        }

        if( aNetlist.IsFindByTimeStamp() )
            footprint = FindModule( aNetlist.GetComponent( i )->GetTimeStamp(), true );
        else
            footprint = FindModule( aNetlist.GetComponent( i )->GetReference() );

        if( footprint == NULL )        // A new footprint.
        {
            if( aReporter )
            {
                if( component->GetModule() != NULL )
                {
                    msg.Printf( _( "Adding new component \"%s:%s\" footprint \"%s\".\n" ),
                                GetChars( component->GetReference() ),
                                GetChars( component->GetTimeStamp() ),
                                GetChars( FROM_UTF8( component->GetFPID().Format() ) ) );

                    aReporter->Report( msg, REPORTER::RPT_ACTION );
                }
                else
                {
                    msg.Printf( _( "Cannot add new component \"%s:%s\" due to missing "
                                   "footprint \"%s\".\n" ),
                                GetChars( component->GetReference() ),
                                GetChars( component->GetTimeStamp() ),
                                GetChars( FROM_UTF8( component->GetFPID().Format() ) ) );

                    aReporter->Report( msg, REPORTER::RPT_ERROR );
                }
            }

            if( !aNetlist.IsDryRun() && (component->GetModule() != NULL) )
            {
                // Owned by NETLIST, can only copy it.
                footprint = new MODULE( *component->GetModule() );
                footprint->SetParent( this );
                footprint->SetPosition( bestPosition );
                footprint->SetTimeStamp( GetNewTimeStamp() );
                newFootprints.push_back( footprint );
                Add( footprint, ADD_APPEND );
                m_connectivity->Add( footprint );
            }
        }
        else                           // An existing footprint.
        {
            // Test for footprint change.
            if( !component->GetFPID().empty() &&
                footprint->GetFPID() != component->GetFPID() )
            {
                if( aNetlist.GetReplaceFootprints() )
                {
                    if( aReporter )
                    {
                        if( component->GetModule() != NULL )
                        {
                            msg.Printf( _( "Replacing component \"%s:%s\" footprint \"%s\" with "
                                           "\"%s\".\n" ),
                                        GetChars( footprint->GetReference() ),
                                        GetChars( footprint->GetPath() ),
                                        GetChars( FROM_UTF8( footprint->GetFPID().Format() ) ),
                                        GetChars( FROM_UTF8( component->GetFPID().Format() ) ) );

                            aReporter->Report( msg, REPORTER::RPT_ACTION );
                        }
                        else
                        {
                            msg.Printf( _( "Cannot replace component \"%s:%s\" due to missing "
                                           "footprint \"%s\".\n" ),
                                        GetChars( footprint->GetReference() ),
                                        GetChars( footprint->GetPath() ),
                                        GetChars( FROM_UTF8( component->GetFPID().Format() ) ) );

                            aReporter->Report( msg, REPORTER::RPT_ERROR );
                        }
                    }

                    if( !aNetlist.IsDryRun() && (component->GetModule() != NULL) )
                    {
                        wxASSERT( footprint != NULL );
                        MODULE* newFootprint = new MODULE( *component->GetModule() );

                        if( aNetlist.IsFindByTimeStamp() )
                            newFootprint->SetReference( footprint->GetReference() );
                        else
                            newFootprint->SetPath( footprint->GetPath() );

                        // Copy placement and pad net names.
                        // optionally, copy or not local settings (like local clearances)
                        // if the second parameter is "true", previous values will be used.
                        // if "false", the default library values of the new footprint
                        // will be used
                        footprint->CopyNetlistSettings( newFootprint, false );

                        // Compare the footprint name only, in case the nickname is empty or in case
                        // user moved the footprint to a new library.  Chances are if footprint name is
                        // same then the footprint is very nearly the same and the two texts should
                        // be kept at same size, position, and rotation.
                        if( newFootprint->GetFPID().GetLibItemName() == footprint->GetFPID().GetLibItemName() )
                        {
                            newFootprint->Reference().SetEffects( footprint->Reference() );
                            newFootprint->Value().SetEffects( footprint->Value() );
                        }

                        m_connectivity->Remove( footprint );
                        Remove( footprint );

                        Add( newFootprint, ADD_APPEND );
                        m_connectivity->Add( footprint );

                        footprint = newFootprint;
                    }
                }
            }

            // Test for reference designator field change.
            if( footprint->GetReference() != component->GetReference() )
            {
                if( aReporter )
                {
                    msg.Printf( _( "Changing component \"%s:%s\" reference to \"%s\".\n" ),
                                GetChars( footprint->GetReference() ),
                                GetChars( footprint->GetPath() ),
                                GetChars( component->GetReference() ) );
                    aReporter->Report( msg, REPORTER::RPT_ACTION );
                }

                if( !aNetlist.IsDryRun() )
                    footprint->SetReference( component->GetReference() );
            }

            // Test for value field change.
            if( footprint->GetValue() != component->GetValue() )
            {
                if( aReporter )
                {
                    msg.Printf( _( "Changing component \"%s:%s\" value from \"%s\" to \"%s\".\n" ),
                                GetChars( footprint->GetReference() ),
                                GetChars( footprint->GetPath() ),
                                GetChars( footprint->GetValue() ),
                                GetChars( component->GetValue() ) );
                    aReporter->Report( msg, REPORTER::RPT_ACTION );
                }

                if( !aNetlist.IsDryRun() )
                    footprint->SetValue( component->GetValue() );
            }

            // Test for time stamp change.
            if( footprint->GetPath() != component->GetTimeStamp() )
            {
                if( aReporter )
                {
                    msg.Printf( _( "Changing component path \"%s:%s\" to \"%s\".\n" ),
                                GetChars( footprint->GetReference() ),
                                GetChars( footprint->GetPath() ),
                                GetChars( component->GetTimeStamp() ) );
                    aReporter->Report( msg, REPORTER::RPT_INFO );
                }

                if( !aNetlist.IsDryRun() )
                    footprint->SetPath( component->GetTimeStamp() );
            }
        }

        if( footprint == NULL )
            continue;

        // At this point, the component footprint is updated.  Now update the nets.
        for( auto pad : footprint->Pads() )
        {
            COMPONENT_NET net = component->GetNet( pad->GetPadName() );

            if( !net.IsValid() )                // Footprint pad had no net.
            {
                if( aReporter && !pad->GetNetname().IsEmpty() )
                {
                    msg.Printf( _( "Clearing component \"%s:%s\" pin \"%s\" net name.\n" ),
                                GetChars( footprint->GetReference() ),
                                GetChars( footprint->GetPath() ),
                                GetChars( pad->GetPadName() ) );
                    aReporter->Report( msg, REPORTER::RPT_ACTION );
                }

                if( !aNetlist.IsDryRun() )
                {
                    m_connectivity->Remove( pad );
                    pad->SetNetCode( NETINFO_LIST::UNCONNECTED );
                }
            }
            else                                 // Footprint pad has a net.
            {
                if( net.GetNetName() != pad->GetNetname() )
                {
                    if( aReporter )
                    {
                        msg.Printf( _( "Changing component \"%s:%s\" pin \"%s\" net name from "
                                       "\"%s\" to \"%s\".\n" ),
                                    GetChars( footprint->GetReference() ),
                                    GetChars( footprint->GetPath() ),
                                    GetChars( pad->GetPadName() ),
                                    GetChars( pad->GetNetname() ),
                                    GetChars( net.GetNetName() ) );
                        aReporter->Report( msg, REPORTER::RPT_ACTION );
                    }

                    if( !aNetlist.IsDryRun() )
                    {
                        NETINFO_ITEM* netinfo = FindNet( net.GetNetName() );

                        if( netinfo == NULL )
                        {
                            // It is a new net, we have to add it
                            netinfo = new NETINFO_ITEM( this, net.GetNetName() );
                            Add( netinfo );
                        }

                        m_connectivity->Remove( pad );
                        pad->SetNetCode( netinfo->GetNet() );
                        m_connectivity->Add( pad );
                    }
                }
            }
        }
    }

    // Remove all components not in the netlist.
    if( aNetlist.GetDeleteExtraFootprints() )
    {
        MODULE* nextModule;
        const COMPONENT* component;

        for( MODULE* module = m_Modules;  module != NULL;  module = nextModule )
        {
            nextModule = module->Next();

            if( module->IsLocked() )
                continue;

            if( aNetlist.IsFindByTimeStamp() )
                component = aNetlist.GetComponentByTimeStamp( module->GetPath() );
            else
                component = aNetlist.GetComponentByReference( module->GetReference() );

            if( component == NULL )
            {
                if( aReporter )
                {
                    msg.Printf( _( "Removing unused component \"%s:%s\".\n" ),
                                GetChars( module->GetReference() ),
                                GetChars( module->GetPath() ) );
                    aReporter->Report( msg, REPORTER::RPT_ACTION );
                }

                if( !aNetlist.IsDryRun() )
                {
                    m_connectivity->Remove( module );
                    module->DeleteStructure();
                }
            }
        }
    }

    BuildListOfNets();
    std::vector<D_PAD*> padlist = GetPads();
    auto connAlgo = m_connectivity->GetConnectivityAlgo();

    // If needed, remove the single pad nets:
    if( aDeleteSinglePadNets && !aNetlist.IsDryRun() )
    {
        std::vector<unsigned int> padCount( connAlgo->NetCount() );

        for( const auto cnItem : connAlgo->PadList() )
        {
            int net = cnItem->Parent()->GetNetCode();

            if( net > 0 )
                ++padCount[net];
        }

        for( i = 0; i < (unsigned)connAlgo->NetCount(); ++i )
        {
            // First condition: only one pad in the net
            if( padCount[i] == 1 )
            {
                // Second condition, no zones attached to the pad
                D_PAD* pad = nullptr;
                int zoneCount = 0;
                const KICAD_T types[] = { PCB_PAD_T, PCB_ZONE_AREA_T, EOT };
                auto netItems = m_connectivity->GetNetItems( i, types );

                for( const auto item : netItems )
                {
                    if( item->Type() == PCB_ZONE_AREA_T )
                    {
                        wxASSERT( !pad || pad->GetNet() == item->GetNet() );
                        ++zoneCount;
                    }
                    else if( item->Type() == PCB_PAD_T )
                    {
                        wxASSERT( !pad );
                        pad = static_cast<D_PAD*>( item );
                    }
                }

                wxASSERT( pad );

                if( zoneCount == 0 )
                {
                    if( aReporter )
                    {
                        msg.Printf( _( "Remove single pad net \"%s\" on \"%s\" pad '%s'\n" ),
                                    GetChars( pad->GetNetname() ),
                                    GetChars( pad->GetParent()->GetReference() ),
                                    GetChars( pad->GetPadName() ) );
                        aReporter->Report( msg, REPORTER::RPT_ACTION );
                    }

                    m_connectivity->Remove( pad );
                    pad->SetNetCode( NETINFO_LIST::UNCONNECTED );
                }
            }
        }
    }

    // Last step: Some tests:
    // verify all pads found in netlist:
    // They should exist in footprints, otherwise the footprint is wrong
    // note also references or time stamps are updated, so we use only
    // the reference to find a footprint
    //
    // Also verify if zones have acceptable nets, i.e. nets with pads.
    // Zone with no pad belongs to a "dead" net which happens after changes in schematic
    // when no more pad use this net name.
    if( aReporter )
    {
        wxString padname;
        for( i = 0; i < aNetlist.GetCount(); i++ )
        {
            const COMPONENT* component = aNetlist.GetComponent( i );
            MODULE* footprint = FindModuleByReference( component->GetReference() );

            if( footprint == NULL )    // It can be missing in partial designs
                continue;

            // Explore all pins/pads in component
            for( unsigned jj = 0; jj < component->GetNetCount(); jj++ )
            {
                COMPONENT_NET net = component->GetNet( jj );
                padname = net.GetPinName();

                if( footprint->FindPadByName( padname ) )
                    continue;   // OK, pad found

                // not found: bad footprint, report error
                msg.Printf( _( "Component '%s' pad '%s' not found in footprint '%s'\n" ),
                            GetChars( component->GetReference() ),
                            GetChars( padname ),
                            GetChars( FROM_UTF8( footprint->GetFPID().Format() ) ) );
                aReporter->Report( msg, REPORTER::RPT_ERROR );
            }
        }

        // Test copper zones to detect "dead" nets (nets without any pad):
        for( int ii = 0; ii < GetAreaCount(); ii++ )
        {
            ZONE_CONTAINER* zone = GetArea( ii );

            if( !zone->IsOnCopperLayer() || zone->GetIsKeepout() )
                continue;

            if( m_connectivity->GetPadCount( zone->GetNetCode() ) == 0 )
            {
                msg.Printf( _( "Copper zone (net name '%s'): net has no pads connected." ),
                           GetChars( zone->GetNet()->GetNetname() ) );
                aReporter->Report( msg, REPORTER::RPT_WARNING );
            }
        }
    }

    m_connectivity->RecalculateRatsnest();

    std::swap( newFootprints, *aNewFootprints );
}


BOARD_ITEM* BOARD::Duplicate( const BOARD_ITEM* aItem,
                              bool aAddToBoard )
{
    BOARD_ITEM* new_item = NULL;

    switch( aItem->Type() )
    {
    case PCB_MODULE_T:
    case PCB_TEXT_T:
    case PCB_LINE_T:
    case PCB_TRACE_T:
    case PCB_VIA_T:
    case PCB_ZONE_AREA_T:
    case PCB_TARGET_T:
    case PCB_DIMENSION_T:
        new_item = static_cast<BOARD_ITEM*>( aItem->Clone() );
        break;

    default:
        // Un-handled item for duplication
        new_item = NULL;
        break;
    }

    if( new_item && aAddToBoard )
        Add( new_item );

    return new_item;
}


/* Extracts the board outlines and build a closed polygon
 * from lines, arcs and circle items on edge cut layer
 * Any closed outline inside the main outline is a hole
 * All contours should be closed, i.e. are valid vertices for a closed polygon
 * return true if success, false if a contour is not valid
 */
extern bool BuildBoardPolygonOutlines( BOARD* aBoard,
                                SHAPE_POLY_SET& aOutlines,
                                wxString* aErrorText );


bool BOARD::GetBoardPolygonOutlines( SHAPE_POLY_SET& aOutlines,
                                     wxString* aErrorText )
{
    bool success = BuildBoardPolygonOutlines( this, aOutlines, aErrorText );

    // Make polygon strictly simple to avoid issues (especially in 3D viewer)
    aOutlines.Simplify( SHAPE_POLY_SET::PM_STRICTLY_SIMPLE );

    return success;

}


const std::vector<D_PAD*> BOARD::GetPads()
{
    std::vector<D_PAD*> rv;
    for ( auto mod: Modules() )
    {
        for ( auto pad: mod->Pads() )
            rv.push_back ( pad );

    }

    return rv;
}


unsigned BOARD::GetPadCount() const
{
    return m_connectivity->GetPadCount();
}


/**
 * Function GetPad
 * @return D_PAD* - at the \a aIndex
 */
D_PAD* BOARD::GetPad( unsigned aIndex ) const
{
    unsigned count = 0;

    for( MODULE* mod = m_Modules; mod ; mod = mod->Next() ) // FIXME: const DLIST_ITERATOR
    {
        for( D_PAD* pad = mod->PadsList(); pad; pad = pad->Next() )
        {
            if( count == aIndex )
                return pad;

            count++;
        }
    }

    return nullptr;
}
