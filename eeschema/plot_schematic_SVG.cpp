/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2009 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2011-2016 Wayne Stambaugh <stambaughw@verizon.net>
 * Copyright (C) 1992-2016 KiCad Developers, see AUTHORS.txt for contributors.
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
 * @file plot_schematic_SVG.cpp
 */

#include <fctsys.h>
#include <pgm_base.h>
#include <class_drawpanel.h>
#include <class_sch_screen.h>
#include <schframe.h>
#include <base_units.h>
#include <libeditframe.h>
#include <sch_sheet_path.h>
#include <project.h>
#include <reporter.h>

#include <dialog_plot_schematic.h>
#include <wx_html_report_panel.h>

void DIALOG_PLOT_SCHEMATIC::createSVGFile( bool aPrintAll, bool aPrintFrameRef )
{
    wxString        msg;
    REPORTER&       reporter = m_MessagesBox->Reporter();
    SCH_SHEET_PATH  oldsheetpath = m_parent->GetCurrentSheet();
    SCH_SHEET_LIST  sheetList;

    if( aPrintAll )
        sheetList.BuildSheetList( g_RootSheet );
    else
        sheetList.push_back( m_parent->GetCurrentSheet() );

    for( unsigned i = 0; i < sheetList.size(); i++ )
    {
        SCH_SCREEN*  screen;
        m_parent->SetCurrentSheet( sheetList[i] );
        m_parent->GetCurrentSheet().UpdateAllScreenReferences();
        m_parent->SetSheetNumberAndCount();
        screen = m_parent->GetCurrentSheet().LastScreen();

        try
        {
            wxString fname = m_parent->GetUniqueFilenameForCurrentSheet();
            wxString ext = SVG_PLOTTER::GetDefaultFileExtension();
            wxFileName plotFileName = createPlotFileName( m_outputDirectoryName,
                                                          fname, ext, &reporter );

            bool success = plotOneSheetSVG( m_parent, plotFileName.GetFullPath(), screen,
                                            getModeColor() ? false : true,
                                            aPrintFrameRef );

            if( !success )
            {
                msg.Printf( _( "Cannot create file '%s'.\n" ),
                            GetChars( plotFileName.GetFullPath() ) );
                reporter.Report( msg, REPORTER::RPT_ERROR );
            }
            else
            {
                msg.Printf( _( "Plot: '%s' OK.\n" ),
                            GetChars( plotFileName.GetFullPath() ) );
                reporter.Report( msg, REPORTER::RPT_ACTION );
            }
        }
        catch( const IO_ERROR& e )
        {
            // Cannot plot SVG file
            msg.Printf( wxT( "SVG Plotter exception: %s" ), GetChars( e.What() ) );
            reporter.Report( msg, REPORTER::RPT_ERROR );
            break;
        }
    }

    m_parent->SetCurrentSheet( oldsheetpath );
    m_parent->GetCurrentSheet().UpdateAllScreenReferences();
    m_parent->SetSheetNumberAndCount();
}


bool DIALOG_PLOT_SCHEMATIC::plotOneSheetSVG( EDA_DRAW_FRAME*    aFrame,
                                             const wxString&    aFileName,
                                             SCH_SCREEN*        aScreen,
                                             bool               aPlotBlackAndWhite,
                                             bool               aPlotFrameRef )
{
    SVG_PLOTTER* plotter = new SVG_PLOTTER();

    const PAGE_INFO&   pageInfo = aScreen->GetPageSettings();
    plotter->SetPageSettings( pageInfo );
    plotter->SetDefaultLineWidth( GetDefaultLineThickness() );
    plotter->SetColorMode( aPlotBlackAndWhite ? false : true );
    wxPoint plot_offset;
    double scale = 1.0;
    // Currently, plot units are in decimil
    plotter->SetViewport( plot_offset, IU_PER_MILS/10, scale, false );

    // Init :
    plotter->SetCreator( wxT( "Eeschema-SVG" ) );

    if( ! plotter->OpenFile( aFileName ) )
    {
        delete plotter;
        return false;
    }

    plotter->StartPlot();

    if( aPlotFrameRef )
    {
        plotter->SetColor( BLACK );
        PlotWorkSheet( plotter, aFrame->GetTitleBlock(),
                       aFrame->GetPageSettings(),
                       aScreen->m_ScreenNumber, aScreen->m_NumberOfScreens,
                       aFrame->GetScreenDesc(),
                       aScreen->GetFileName() );
    }

    aScreen->Plot( plotter );

    plotter->EndPlot();
    delete plotter;

    return true;
}
