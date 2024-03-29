/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004 Jean-Pierre Charras, jaen-pierre.charras@gipsa-lab.inpg.com
 * Copyright (C) 2008-2011 Wayne Stambaugh <stambaughw@verizon.net>
 * Copyright (C) 2004-2011 KiCad Developers, see change_log.txt for contributors.
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
 * @file eeschema.cpp
 * @brief the main file
 */

#include <fctsys.h>
#include <pgm_base.h>
#include <kiface_i.h>
#include <class_drawpanel.h>
#include <confirm.h>
#include <gestfich.h>
#include <eda_dde.h>
#include <schframe.h>
#include <libeditframe.h>
#include <viewlib_frame.h>
#include <eda_text.h>

#include <general.h>
#include <class_libentry.h>
#include <hotkeys.h>
#include <transform.h>
#include <wildcards_and_files_ext.h>
#include <symbol_lib_table.h>

#include <kiway.h>
#include <sim/sim_plot_frame.h>

// The main sheet of the project
SCH_SHEET*  g_RootSheet = NULL;

// a transform matrix, to display components in lib editor
TRANSFORM DefaultTransform = TRANSFORM( 1, 0, 0, -1 );


namespace SCH {

static struct IFACE : public KIFACE_I
{
    // Of course all are virtual overloads, implementations of the KIFACE.

    IFACE( const char* aName, KIWAY::FACE_T aType ) :
        KIFACE_I( aName, aType )
    {}

    bool OnKifaceStart( PGM_BASE* aProgram, int aCtlBits ) override;

    void OnKifaceEnd() override;

    wxWindow* CreateWindow( wxWindow* aParent, int aClassId, KIWAY* aKiway, int aCtlBits = 0 ) override
    {
        switch( aClassId )
        {
        case FRAME_SCH:
            {
                SCH_EDIT_FRAME* frame = new SCH_EDIT_FRAME( aKiway, aParent );

                if( Kiface().IsSingle() )
                {
                    // only run this under single_top, not under a project manager.
                    CreateServer( frame, KICAD_SCH_PORT_SERVICE_NUMBER );
                }
                return frame;
            }
            break;

        case FRAME_SCH_LIB_EDITOR:
            {
                LIB_EDIT_FRAME* frame = new LIB_EDIT_FRAME( aKiway, aParent );
                return frame;
            }
            break;

#ifdef KICAD_SPICE
        case FRAME_SIMULATOR:
            {
                SIM_PLOT_FRAME* frame = new SIM_PLOT_FRAME( aKiway, aParent );
                return frame;
            }
            break;
#endif /* KICAD_SPICE */

        case FRAME_SCH_VIEWER:
        case FRAME_SCH_VIEWER_MODAL:
            {
                LIB_VIEW_FRAME* frame = new LIB_VIEW_FRAME( aKiway, aParent, FRAME_T( aClassId ) );
                return frame;
            }
            break;

        default:
            return NULL;
        }
    }

    /**
     * Function IfaceOrAddress
     * return a pointer to the requested object.  The safest way to use this
     * is to retrieve a pointer to a static instance of an interface, similar to
     * how the KIFACE interface is exported.  But if you know what you are doing
     * use it to retrieve anything you want.
     *
     * @param aDataId identifies which object you want the address of.
     *
     * @return void* - and must be cast into the know type.
     */
    void* IfaceOrAddress( int aDataId ) override
    {
        return NULL;
    }

} kiface( "eeschema", KIWAY::FACE_SCH );

} // namespace

using namespace SCH;

static PGM_BASE* process;


KIFACE_I& Kiface() { return kiface; }


// KIFACE_GETTER's actual spelling is a substitution macro found in kiway.h.
// KIFACE_GETTER will not have name mangling due to declaration in kiway.h.
MY_API( KIFACE* ) KIFACE_GETTER(  int* aKIFACEversion, int aKiwayVersion, PGM_BASE* aProgram )
{
    process = aProgram;
    return &kiface;
}


PGM_BASE& Pgm()
{
    wxASSERT( process );    // KIFACE_GETTER has already been called.
    return *process;
}


static COLOR4D s_layerColor[SCH_LAYER_ID_COUNT];

COLOR4D GetLayerColor( SCH_LAYER_ID aLayer )
{
    unsigned layer = SCH_LAYER_INDEX( aLayer );
    wxASSERT( layer < DIM( s_layerColor ) );
    return s_layerColor[layer];
}

void SetLayerColor( COLOR4D aColor, SCH_LAYER_ID aLayer )
{
    unsigned layer = SCH_LAYER_INDEX( aLayer );
    wxASSERT( layer < DIM( s_layerColor ) );
    s_layerColor[layer] = aColor;
}


static PARAM_CFG_ARRAY& cfg_params()
{
    static PARAM_CFG_ARRAY ca;

    if( !ca.size() )
    {
        // These are KIFACE specific, they need to be loaded once when the
        // eeschema KIFACE comes in.

#define CLR(x, y, z)\
    ca.push_back( new PARAM_CFG_SETCOLOR( true, wxT( x ),\
                                          &s_layerColor[SCH_LAYER_INDEX( y )], z ) );

        CLR( "ColorWireEx",             LAYER_WIRE,                 COLOR4D( GREEN ) )
        CLR( "ColorBusEx",              LAYER_BUS,                  COLOR4D( BLUE ) )
        CLR( "ColorConnEx",             LAYER_JUNCTION,             COLOR4D( GREEN ) )
        CLR( "ColorLLabelEx",           LAYER_LOCLABEL,             COLOR4D( BLACK ) )
        CLR( "ColorHLabelEx",           LAYER_HIERLABEL,            COLOR4D( BROWN ) )
        CLR( "ColorGLabelEx",           LAYER_GLOBLABEL,            COLOR4D( RED ) )
        CLR( "ColorPinNumEx",           LAYER_PINNUM,               COLOR4D( RED ) )
        CLR( "ColorPinNameEx",          LAYER_PINNAM,               COLOR4D( CYAN ) )
        CLR( "ColorFieldEx",            LAYER_FIELDS,               COLOR4D( MAGENTA ) )
        CLR( "ColorReferenceEx",        LAYER_REFERENCEPART,        COLOR4D( CYAN ) )
        CLR( "ColorValueEx",            LAYER_VALUEPART,            COLOR4D( CYAN ) )
        CLR( "ColorNoteEx",             LAYER_NOTES,                COLOR4D( LIGHTBLUE ) )
        CLR( "ColorBodyEx",             LAYER_DEVICE,               COLOR4D( RED ) )
        CLR( "ColorBodyBgEx",           LAYER_DEVICE_BACKGROUND,    COLOR4D( LIGHTYELLOW ) )
        CLR( "ColorNetNameEx",          LAYER_NETNAM,               COLOR4D( DARKGRAY ) )
        CLR( "ColorPinEx",              LAYER_PIN,                  COLOR4D( RED ) )
        CLR( "ColorSheetEx",            LAYER_SHEET,                COLOR4D( MAGENTA ) )
        CLR( "ColorSheetFileNameEx",    LAYER_SHEETFILENAME,        COLOR4D( BROWN ) )
        CLR( "ColorSheetNameEx",        LAYER_SHEETNAME,            COLOR4D( CYAN ) )
        CLR( "ColorSheetLabelEx",       LAYER_SHEETLABEL,           COLOR4D( BROWN ) )
        CLR( "ColorNoConnectEx",        LAYER_NOCONNECT,            COLOR4D( BLUE ) )
        CLR( "ColorErcWEx",             LAYER_ERC_WARN,             COLOR4D( GREEN ) )
        CLR( "ColorErcEEx",             LAYER_ERC_ERR,              COLOR4D( RED ) )
        CLR( "ColorGridEx",             LAYER_SCHEMATIC_GRID,       COLOR4D( DARKGRAY ) )
        CLR( "ColorBgCanvasEx",         LAYER_SCHEMATIC_BACKGROUND, COLOR4D( WHITE ) )
        CLR( "ColorBrighenedEx",        LAYER_BRIGHTENED,           COLOR4D( PUREMAGENTA ) )
    }

    return ca;
}


bool IFACE::OnKifaceStart( PGM_BASE* aProgram, int aCtlBits )
{
    // This is process level, not project level, initialization of the DSO.

    // Do nothing in here pertinent to a project!

    start_common( aCtlBits );

    // Give a default colour for all layers
    // (actual color will be initialized by config)
    for( SCH_LAYER_ID ii = SCH_LAYER_ID_START; ii < SCH_LAYER_ID_END; ++ii )
        SetLayerColor( COLOR4D( DARKGRAY ), ii );

    SetLayerColor( COLOR4D::WHITE, LAYER_SCHEMATIC_BACKGROUND );

    // Must be called before creating the main frame in order to
    // display the real hotkeys in menus or tool tips
    ReadHotkeyConfig( SCH_EDIT_FRAME_NAME, g_Eeschema_Hokeys_Descr );

    wxConfigLoadSetups( KifaceSettings(), cfg_params() );

    try
    {
        // The global table is not related to a specific project.  All projects
        // will use the same global table.  So the KIFACE::OnKifaceStart() contract
        // of avoiding anything project specific is not violated here.
        if( !SYMBOL_LIB_TABLE::LoadGlobalTable( SYMBOL_LIB_TABLE::GetGlobalLibTable() ) )
        {
            DisplayInfoMessage( NULL, _(
                "You have run Eeschema for the first time using the new symbol library table "
                "method for finding symbols.\n\n"
                "Eeschema has either copied the default table or created an empty table in the "
                "kicad configuration folder.\n\n"
                "You must first configure the library table to include all symbol libraries you "
                "want to use.\n\n"
                "See the \"Symbol Library Table\" section of Eeschema documentation for more "
                "information." ) );
        }
    }
    catch( const IO_ERROR& ioe )
    {
        // if we are here, a incorrect global symbol library table was found.
        // Incorrect global symbol library table is not a fatal error:
        // the user just has to edit the (partially) loaded table.
        wxString msg = wxString::Format( _(
            "An error occurred attempting to load the global symbol library table:"
            "\n\n%s\n\n"
            "Please edit this global symbol library table in Preferences menu" ),
            GetChars( ioe.What() )
            );
        DisplayError( NULL, msg );
    }

    return true;
}


void IFACE::OnKifaceEnd()
{
    wxConfigSaveSetups( KifaceSettings(), cfg_params() );
    end_common();
}
