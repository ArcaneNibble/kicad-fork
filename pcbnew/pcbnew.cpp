/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2012 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
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

/**
 * @file pcbnew.cpp
 * @brief Pcbnew main program.
 */

#ifdef KICAD_SCRIPTING
 #include <python_scripting.h>
 #include <pcbnew_scripting_helpers.h>
#endif
#include <fctsys.h>
#include <pgm_base.h>
#include <kiface_i.h>
#include <kiface_ids.h>
#include <confirm.h>
#include <macros.h>
#include <make_unique.h>
#include <class_drawpanel.h>
#include <wxPcbStruct.h>
#include <eda_dde.h>
#include <colors_selection.h>
#include <wx/stdpaths.h>

#include <wx/file.h>
#include <wx/snglinst.h>
#include <wx/dir.h>
#include <gestfich.h>

#include <pcbnew.h>
#include <hotkeys.h>
#include <wildcards_and_files_ext.h>
#include <class_board.h>
#include <class_draw_panel_gal.h>
#include <fp_lib_table.h>
#include <module_editor_frame.h>
#include <modview_frame.h>
#include <footprint_wizard_frame.h>
#include <footprint_preview_panel.h>
#include <footprint_info_impl.h>
#include <gl_context_mgr.h>
extern bool IsWxPythonLoaded();

// Colors for layers and items
COLORS_DESIGN_SETTINGS g_ColorsSettings;

bool         g_Drc_On = true;
bool         g_AutoDeleteOldTrack = true;
bool         g_Raccord_45_Auto = true;
bool         g_Alternate_Track_Posture = false;
bool         g_Track_45_Only_Allowed = true;  // True to allow horiz, vert. and 45deg only tracks
bool         g_Segments_45_Only;              // True to allow horiz, vert. and 45deg only graphic segments
bool         g_TwoSegmentTrackBuild = true;

PCB_LAYER_ID g_Route_Layer_TOP;
PCB_LAYER_ID g_Route_Layer_BOTTOM;
int          g_MagneticPadOption   = CAPTURE_CURSOR_IN_TRACK_TOOL;
int          g_MagneticTrackOption = CAPTURE_CURSOR_IN_TRACK_TOOL;

wxPoint     g_Offset_Module;     // module offset used when moving a footprint

/* Name of the document footprint list
 * usually located in share/modules/footprints_doc
 * this is of the responsibility to users to create this file
 * if they want to have a list of footprints
 */
wxString    g_DocModulesFileName = wxT( "footprints_doc/footprints.pdf" );

/*
 * Used in track creation, a list of track segments currently being created,
 * with the newest track at the end of the list, sorted by new-ness.  e.g. use
 * TRACK->Back() to get the next older track, TRACK->Next() to get the next
 * newer track.
 */
DLIST<TRACK> g_CurrentTrackList;


namespace PCB {

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
        wxWindow* frame = NULL;

        switch( aClassId )
        {
        case FRAME_PCB:
            frame = dynamic_cast< wxWindow* >( new PCB_EDIT_FRAME( aKiway, aParent ) );

#if defined( KICAD_SCRIPTING )
            // give the scripting helpers access to our frame
            ScriptingSetPcbEditFrame( (PCB_EDIT_FRAME*) frame );
#endif

            if( Kiface().IsSingle() )
            {
                // only run this under single_top, not under a project manager.
                CreateServer( frame, KICAD_PCB_PORT_SERVICE_NUMBER );
            }

            break;

        case FRAME_PCB_MODULE_EDITOR:
            frame = dynamic_cast< wxWindow* >( new FOOTPRINT_EDIT_FRAME( aKiway, aParent ) );
            break;

        case FRAME_PCB_MODULE_VIEWER:
        case FRAME_PCB_MODULE_VIEWER_MODAL:
            frame = dynamic_cast< wxWindow* >( new FOOTPRINT_VIEWER_FRAME( aKiway, aParent,
                                                                           FRAME_T( aClassId ) ) );
            break;

        case FRAME_PCB_FOOTPRINT_WIZARD_MODAL:
            frame = dynamic_cast< wxWindow* >( new FOOTPRINT_WIZARD_FRAME( aKiway, aParent,
                                                                           FRAME_T( aClassId ) ) );
            break;

        case FRAME_PCB_FOOTPRINT_PREVIEW:
            frame = dynamic_cast< wxWindow* >( FOOTPRINT_PREVIEW_PANEL::New( aKiway, aParent ) );
            break;

        default:
            break;
        }

        return frame;
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
        switch( aDataId )
        {
        case KIFACE_NEW_FOOTPRINT_LIST:
            return (void*) static_cast<FOOTPRINT_LIST*>( new FOOTPRINT_LIST_IMPL() );

        case KIFACE_G_FOOTPRINT_TABLE:
            return (void*) new FP_LIB_TABLE( &GFootprintTable );

        default:
            return nullptr;
        }
    }

} kiface( "pcbnew", KIWAY::FACE_PCB );

} // namespace

using namespace PCB;


static PGM_BASE* process;


KIFACE_I& Kiface() { return kiface; }


// KIFACE_GETTER's actual spelling is a substitution macro found in kiway.h.
// KIFACE_GETTER will not have name mangling due to declaration in kiway.h.
MY_API( KIFACE* ) KIFACE_GETTER( int* aKIFACEversion, int aKiwayVersion, PGM_BASE* aProgram )
{
    process = aProgram;
    return &kiface;
}

#if defined( BUILD_KIWAY_DLL )
PGM_BASE& Pgm()
{
    wxASSERT( process );    // KIFACE_GETTER has already been called.
    return *process;
}
#endif


#if defined( KICAD_SCRIPTING )
static bool scriptingSetup()
{

#if defined( __WINDOWS__ )
    // If our python.exe (in kicad/bin) exists, force our kicad python environment
    wxString kipython = FindKicadFile( "python.exe" );

    // we need only the path:
    wxFileName fn( kipython  );
    kipython = fn.GetPath();

    // If our python install is existing inside kicad, use it
    // Note: this is usefull only when an other python version is installed
    if( wxDirExists( kipython ) )
    {
        // clear any PYTHONPATH and PYTHONHOME env var definition: the default
        // values work fine inside Kicad:
        wxSetEnv( wxT( "PYTHONPATH" ), wxEmptyString );
        wxSetEnv( wxT( "PYTHONHOME" ), wxEmptyString );

        // Add our python executable path in first position:
        wxString ppath;
        wxGetEnv( wxT( "PATH" ), &ppath );

        kipython << wxT( ";" ) << ppath;
        wxSetEnv( wxT( "PATH" ), kipython );
    }

#elif defined( __WXMAC__ )

    // Add default paths to PYTHONPATH
    wxString pypath;

    // Bundle scripting folder (<kicad.app>/Contents/SharedSupport/scripting)
    pypath += GetOSXKicadDataDir() + wxT( "/scripting" );

    // $(KICAD_PATH)/scripting/plugins is always added in kicadplugins.i
    if( wxGetenv("KICAD_PATH") != NULL )
    {
        pypath += wxT( ":" ) + wxString( wxGetenv("KICAD_PATH") );
    }

    // Bundle wxPython folder (<kicad.app>/Contents/Frameworks/python/site-packages)
    pypath += wxT( ":" ) + Pgm().GetExecutablePath() +
              wxT( "Contents/Frameworks/python/site-packages" );

    // Original content of $PYTHONPATH
    if( wxGetenv( wxT( "PYTHONPATH" ) ) != NULL )
    {
        pypath = wxString( wxGetenv( wxT( "PYTHONPATH" ) ) ) + wxT( ":" ) + pypath;
    }

    // set $PYTHONPATH
    wxSetEnv( "PYTHONPATH", pypath );

#else
    // Linux-specific setup
    wxString pypath;

    pypath = Pgm().GetExecutablePath() + wxT( "../lib/python2.7/dist-packages" );

    if( !wxIsEmpty( wxGetenv( wxT( "PYTHONPATH" ) ) ) )
        pypath = wxString( wxGetenv( wxT( "PYTHONPATH" ) ) ) + wxT( ":" ) + pypath;

    wxSetEnv( wxT( "PYTHONPATH" ), pypath );

#endif

    if( !pcbnewInitPythonScripting( TO_UTF8( PyScriptingPath() ) ) )
    {
        wxLogError( "pcbnewInitPythonScripting() failed." );
        return false;
    }

    return true;
}
#endif  // KICAD_SCRIPTING


void PythonPluginsReloadBase()
{
#if defined(KICAD_SCRIPTING)
    //Reload plugin list: reload Python plugins if they are newer than
    // the already loaded, and load new plugins
    char cmd[1024];

    snprintf( cmd, sizeof(cmd),
            "pcbnew.LoadPlugins(\"%s\")", TO_UTF8( PyScriptingPath() ) );

    PyLOCK lock;

    // ReRun the Python method pcbnew.LoadPlugins
    // (already called when starting Pcbnew)
    PyRun_SimpleString( cmd );
#endif
}


/// The global footprint library table.  This is not dynamically allocated because
/// in a multiple project environment we must keep its address constant (since it is
/// the fallback table for multiple projects).
FP_LIB_TABLE    GFootprintTable;


bool IFACE::OnKifaceStart( PGM_BASE* aProgram, int aCtlBits )
{
    // This is process level, not project level, initialization of the DSO.

    // Do nothing in here pertinent to a project!

    start_common( aCtlBits );

    // Must be called before creating the main frame in order to
    // display the real hotkeys in menus or tool tips
    ReadHotkeyConfig( PCB_EDIT_FRAME_NAME, g_Board_Editor_Hokeys_Descr );

    try
    {
        // The global table is not related to a specific project.  All projects
        // will use the same global table.  So the KIFACE::OnKifaceStart() contract
        // of avoiding anything project specific is not violated here.

        if( !FP_LIB_TABLE::LoadGlobalTable( GFootprintTable ) )
        {
            DisplayInfoMessage( NULL, _(
                "You have run Pcbnew for the first time using the "
                "new footprint library table method for finding footprints.\n"
                "Pcbnew has either copied the default "
                "table or created an empty table in the kicad configuration folder.\n"
                "You must first configure the library "
                "table to include all footprint libraries you want to use.\n"
                "See the \"Footprint Library Table\" section of "
                "the CvPcb or Pcbnew documentation for more information." ) );
        }
    }
    catch( const IO_ERROR& ioe )
    {
        // if we are here, a incorrect global footprint library table was found.
        // Incorrect global footprint library table is not a fatal error:
        // the user just has to edit the (partially) loaded table.
        wxString msg = wxString::Format( _(
            "An error occurred attempting to load the global footprint library "
            "table:\n\n%s\n\n"
            "Please edit this global footprint library table in Preferences menu" ),
            GetChars( ioe.What() )
            );
        DisplayError( NULL, msg );
    }

#if defined(KICAD_SCRIPTING)
    scriptingSetup();
#endif

    return true;
}


void IFACE::OnKifaceEnd()
{
    // This function deletes OpenGL contexts used (if any) by wxGLCanvas objects.
    // It can be called only when closing the application, because it deletes an OpenGL context
    // which can still be in usage. Destroying OpenGL contexts earlier may crash the application.
    GL_CONTEXT_MANAGER::Get().DeleteAll();

    end_common();
#if defined( KICAD_SCRIPTING_WXPYTHON )
    // Restore the thread state and tell Python to cleanup after itself.
    // wxPython will do its own cleanup as part of that process.
    // This should only be called if python was setup correctly.

    if( IsWxPythonLoaded() )
        pcbnewFinishPythonScripting();
#endif
}
