
#  This program source code file is part of KICAD, a free EDA CAD application.
#
# Copyright (C) 2011 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
# Copyright (C) 2011-2016 Kicad Developers, see change_log.txt for contributors#.
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, you may find one here:
#  http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
#  or you may search the http://www.gnu.org website for the version 2 license,
#  or you may write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#

# In this directory, a number of different processes are managed:
#
# 1) PNG files are created from SVG files
# 2) CPP files are created from PNG files.
# 3) Object files are created from CPP files.
# 4) A static library is created from the object files

# Step 3) is universal and is done for any builder, whereas steps 1) and 2) are
# optional and depend on MAINTAIN_PNGS being defined in CMake.  The reason we
# can skip 1) and 2) is that the *.CPP files are part of the source tree so
# do not need to rebuilt by a typical builder.  However, because the *.CPP files
# are part of the source tree, and subject to version control, they should be built
# only when needed, otherwise this results in a diff for the version control system.
# Therefore steps 1) and 2) are driven by CMake (if MAINTAIN_PNGS) which gives
# us conditional *.CPP building based on an edit to the respective *.SVG file.

# If MAINTAIN_PNGS is not defined, then you are a normal builder and no special
# tools are required.  If MAINTAIN_PNG is defined, then you are a PNG maintainer
# and will need the following tools findable in your PATH:
#
# 1) inkscape - command line mode is used, must be on your PATH
# 2) pngcrush - this program must be on your PATH
#

# lower case is used for local variables, uppercase for global variables

option( MAINTAIN_PNGS
        "Set to true if you are a PNG maintainer and have the required tools given in the bitmaps_png/CMakeLists.txt file (default OFF)."
        OFF)

# Used Only to maintain PNG files (and therefore to recreate .cpp files)
# The png2cpp creates files with native End of Line format.

include_directories(BEFORE ${INC_BEFORE})
include_directories(
    ${INC_AFTER}
    )




# Plan for three sizes of bitmaps:
# SMALL - for menus         - 16 x 16
# MID   - for toolbars      - 26 x 26
# BIG   - for program icons - 48 x 48
# which are given on three basename lists: BMAPS_SMALL, BMAPS_MID,a nd BMAPS_BIG
# respectively.  The basename is without file extension and without path.
# A corresponding ${basename}.svg file must exist in 'sources' dir below here.


# small icons (16x16) needed in listboxes
set( BMAPS_SMALL
    pinorient_right
    pinorient_left
    pinorient_up
    pinorient_down
    pinshape_normal
    pinshape_invert
    pinshape_clock_normal
    pinshape_clock_invert
    pinshape_active_low_input
    pinshape_clock_active_low
    pinshape_active_low_output
    pinshape_clock_fall
    pinshape_nonlogic
    pintype_input
    pintype_output
    pintype_bidi
    pintype_3states
    pintype_passive
    pintype_notspecif
    pintype_powerinput
    pintype_poweroutput
    pintype_opencoll
    pintype_openemit
    pintype_noconnect
    tree_nosel
    tree_sel
    )


# image basenames that go into the toolbar sized destinations, i.e. 26x26
set( BMAPS_MID
    about
    add_arc
    add_bus2bus
    add_bus
    add_circle
    add_component
    add_corner
    add_dashed_line
    add_dimension
    add_entry
    add_glabel
    add_hierarchical_label
    add_hierar_pin
    add_hierarchical_subsheet
    add_junction
    add_keepout_area
    add_line2bus
    add_line_label
    add_line
    add_mires
    add_polygon
    add_power
    add_rectangle
    add_tracks
    add_via
    add_zone_cutout
    add_zone
    align_items
    anchor
    annotate_down_right
    annotate_right_down
    annotate
    apply_pad_settings
    array_line
    array_module
    array_pad
    array_text
    array_target
    array_zone
    auto_associe
    auto_delete_track
    auto_track_width
    autoplace_fields
    axis3d_back
    axis3d_bottom
    axis3d_front
    axis3d_left
    axis3d_right
    axis3d_top
    axis3d
    bom
    book
    break_bus
    break_line
    browse_files
    cancel
    change_entry_orient
    contrast_mode
    create_cmp_file
    checked_ok
    color_materials
    component_select_unit
    component_select_alternate_shape
    config
    copycomponent
    copy
    copy_library
    copy_pad_settings
    copper_layers_setup
    cursor_shape
    cursor
    cut
    cvpcb
    dashline
    datasheet
    delete_arc
    delete_association
    delete_bus
    delete_circle
    delete_connection
    delete_dimension
    delete_field
    delete_glabel
    delete_line
    delete_module
    delete_net
    delete_node
    delete_pad
    delete_pinsheet
    delete_pin
    delete_polygon
    delete_rectangle
    delete_segment
    delete_sheet
    delete_track
    delete
    directory
    display_options
    distribute_horizontal
    distribute_vertical
    down
    drag
    drag_module
    drag_outline_segment
    drag_pad
    drag_segment_withslope
    drag_track_segment
    drc_off
    drc
    duplicate_line
    duplicate_module
    duplicate_pad
    duplicate_target
    duplicate_text
    duplicate_zone
    edges_sketch
    edit_comp_footprint
    edit_comp_ref
    edit_comp_value
    edit_module
    editor
    edit_text
    edit
    eeschema
    enter_sheet
    ercerr
    erc_green
    ercwarn
    erc
    exit
    exchange
    export_dsn
    export_idf
    export_footprint_names
    export_module
    export
    fabrication
    file_footprint
    fill_zone
    find
    find_replace
    flag
    flip_board
    fonts
    footprint_text
    gbr_select_mode0
    gbr_select_mode1
    gbr_select_mode2
    gerber_file
    gerbview_show_negative_objects
    gerbview_drill_file
    gerbview_clear_layers
    general_deletions
    general_ratsnest
    glabel2label
    glabel2text
    gl_change
    green
    grid_select
    grid_select_axis
    grid
    hammer
    help
    hidden_pin
    hierarchy_nav
    hotkeys
    hotkeys_import
    hotkeys_export
    html
    icon
    icon_cvpcb_small
    icon_gerbview_small
    import3d
    image
    import_brd_file
    import_cmp_from_lib
    import_footprint_names
    import_hierarchical_label
    import_module
    import_setup
    import
    info
    insert_module_board
    invisible_text
    kicad_icon_small
    label2glabel
    label2text
    label
    lang_catalan
    lang_chinese
    lang_bg
    lang_cs
    lang_def
    lang_de
    lang_en
    lang_es
    lang_fr
    lang_fi
    lang_gr
    lang_hu
    lang_it
    lang_jp
    lang_ko
    lang_nl
    lang_pl
    lang_pt
    lang_ru
    lang_sk
    lang_sl
    lang_lt
    language
    layers_manager
    leave_sheet
    left
    libedit
    lib_next
    lib_previous
    library_browse
    library_update
    library
    library_table
    lines90
    list_nets
    load_module_board
    load_module_lib
    local_ratsnest
    locked
    measurement
    mirepcb
    mirror_h
    mirror_v
    mode_module
    mode_track
    modratsnest
    module_check
    module_editor
    module_wizard
    module_filtered_list
    module_full_list
    module_options
    module_pin_filtered_list
    module_library_list
    module_name_filtered_list
    module_ratsnest
    module
    modview_icon
    morgan1
    morgan2
    move_circle
    move_field
    move_glabel
    move_line
    move_module
    move_pad
    move_pinsheet
    move_pin
    move_polygon
    move_rectangle
    move_sheet
    move_target
    move_zone
    move
    mw_add_gap
    mw_add_line
    mw_add_shape
    mw_add_stub_arc
    mw_add_stub
    mw_toolbar
    net_highlight
    net_highlight_schematic
    netlist
    net_locked
    net_unlocked
    new_component
    new_cvpcb
    new_footprint
    new_library
    new_generic
    new_project
    new_project_with_template
    noconn
    normal
    online_help
    open_brd_file
    open_library
    open_project
    open_document
    options_3drender
    options_all_tracks_and_vias
    options_all_tracks
    options_all_vias
    options_arc
    options_circle
    options_module
    options_pad
    options_pinsheet
    options_pin
    options_rectangle
    options_segment
    options_tracks
    options_track
    options_vias
    opt_show_polygon
    orient
    ortho
    pad_sketch
    pad
    pad_dimensions
    pad_enumerate
    pads_mask_layers
    path
    pagelayout_load
    pagelayout_normal_view_mode
    pagelayout_special_view_mode
    palette
    part_properties
    paste
    pcbnew
    pcbcalculator
    pcb_offset
    pin2pin
    pin_name_to
    pin_number_to
    pin_size_to
    pin_show_etype
    pin_table
    pin_to
    pin
    plot
    plot_dxf
    plot_hpg
    plot_pdf
    plot_ps
    plot_svg
    polar_coord
    post_compo
    post_drill
    post_module
    preference
    print_button
    ps_router
    ps_diff_pair
    ps_diff_pair_gap
    ps_diff_pair_tune_length
    ps_diff_pair_tune_phase
    ps_diff_pair_via_gap
    ps_tune_length
    push_pad_settings
    py_script
    ratsnest
    read_setup
    recent
    redo
    red
    reload2
    reload
    render_mode
    reset_text
    resize_sheet
    rescue
    right
    rotate_ccw
    rotate_cw
    rotate_neg_x
    rotate_pos_x
    rotate_neg_y
    rotate_pos_y
    rotate_neg_z
    rotate_pos_z
    save_as
    save_library
    save_part_in_mem
    save_project
    save_setup
    save
    select_grid
    select_layer_pair
    select_w_layer
    shape_3d
    sheetset
    simulator
    sim_run
    sim_stop
    sim_tune
    sim_probe
    sim_add_signal
    sim_settings
    setcolor_3d_bg
    setcolor_silkscreen
    setcolor_soldermask
    setcolor_solderpaste
    setcolor_copper
    setcolor_board_body
    show_dcodenumber
    show_footprint
    show_mod_edge
    showtrack
    show_zone
    show_zone_disable
    show_zone_outline_only
    show_all_layers
    show_no_layers
    show_no_copper_layers
    show_all_copper_layers
    spreadsheet
    svg_file
    swap_layer
    text
    text_sketch
    three_d
    tool_ratsnest
    tools
    track_locked
    track_sketch
    track_unlocked
    transistor
    undelete
    undo
    unit_inch
    unit_mm
    unknown
    unlocked
    unzip
    update_module_board
    up
    use_3D_copper_thickness
    via
    via_buried
    via_microvia
    via_sketch
    warning
    web_support
    width_net
    width_segment
    width_track_via
    width_track
    width_vias
    wizard_add_fplib_small
    zip_tool
    zip
    zone_duplicate
    zone_unfill
    zoom
    zoom_area
    zoom_fit_in_page
    zoom_center_on_screen
    zoom_in
    zoom_out
    zoom_redraw
    zoom_selection
    )


# 48 x 48 for now
set( BMAPS_BIG
    dialog_warning
    icon_3d
    icon_cvpcb
    icon_eeschema
    icon_gerbview
    icon_kicad
    icon_libedit
    icon_modedit
    icon_pcbnew
    icon_bitmap2component
    icon_pcbcalculator
    viewlibs_icon
    icon_pagelayout_editor
    wizard_add_fplib_icon
    )

set( BMAPS_OTHER
    tune_diff_pair_length_legend
    tune_diff_pair_skew_legend
    tune_single_track_length_legend
    )


# @todo keep these in sync with .bzrignore
set( TMP_DIR   "${CMAKE_CURRENT_BINARY_DIR}/tmp" )

# true to generate log files, false to build icon files only
# generate logs is useful only if you have problems
set( CREATE_LOG_FILES false )


function( svg2png inputFile outFile pngWidth pngHeight )
    #message( "svg2png( inputFile: ${inputFile} outFile: ${outFile} pngWidth: ${pngWidth} pngHeight: ${pngHeight})")

    if( CREATE_LOG_FILES )
        set( logfile "${TMP_DIR}/${bmn}.inkscape.log" )
    else()
        set( logfile "null" )
    endif()

    get_filename_component( bmn ${inputFile} NAME_WE )

    add_custom_command(
        OUTPUT ${outFile}
        COMMAND ${Inkscape_EXECUTABLE} --export-area-snap -f ${inputFile} -e ${outFile}
            -w ${pngWidth} -h ${pngHeight} > ${logfile}
        DEPENDS ${inputFile}
        COMMENT "Creating ${pngHeight} pixel tall ${outFile}"
        )
endfunction()


# Function png2png
# converts a basic PNG to one
function( png2png inputFile outFile )

    if( CREATE_LOG_FILES )
        set( logfile "${TMP_DIR}/${bmn}.pngcrush.log" )
    else()
        set( logfile "null" )
    endif()

    get_filename_component( bmn ${inputFile} NAME_WE )

    add_custom_command(
        OUTPUT  ${outFile}

        # pngcrush all icons without background to remove any extraneous text records.
        COMMAND ${pngcrush_EXECUTABLE} -rem alla ${inputFile} ${outFile} > ${logfile}

        DEPENDS ${inputFile}

        COMMENT "Creating cleaned file ${outFile}"
        )
endfunction()

# Function png2cpp
# converts a single *.png to *.cpp
function( png2cpp inputFile outFile )
    add_custom_command(
        OUTPUT  ${outFile}

        COMMAND ${CMAKE_COMMAND} -DinputFile=${inputFile} -DoutCppFile=${outFile}
            -P ${CMAKE_MODULE_PATH}/PNG2cpp.cmake

        DEPENDS ${inputFile} ${CMAKE_MODULE_PATH}/PNG2cpp.cmake
        COMMENT "Creating ${outFile}"
        )
endfunction()


# Function bitmap_dir
# converts all the basenames in bmapList found in hardcoded 'sources' dir
# and and puts them into cpp_${pngHeight} and png_${pngHeight} directories.
function( bitmap_dir pngWidth pngHeight bmapList )

    set( cppDir "${CMAKE_CURRENT_SOURCE_DIR}/cpp_${pngHeight}" )
    set( pngDir "${CMAKE_CURRENT_SOURCE_DIR}/png_${pngHeight}" )

    file( MAKE_DIRECTORY ${pngDir} ${cppDir} )

    #file( REMOVE_RECURSE ${TMP_DIR} )
    file( MAKE_DIRECTORY ${TMP_DIR} )

    #svg2png( inputFile outFile pngWidth pngHeight )

    foreach( bmn ${bmapList} )
        set( pngFile "${pngDir}/${bmn}.png" )
        set( cppFile "${cppDir}/${bmn}.cpp" )
        set( tmpFile "${TMP_DIR}/${bmn}.png" )

        #svg2png( inputFile outFile pngWidth pngHeight )
        svg2png( ${CMAKE_CURRENT_SOURCE_DIR}/sources/${bmn}.svg ${tmpFile} ${pngWidth} ${pngHeight} )

        #png2png( inputFile outFile )
        png2png( ${tmpFile} ${pngFile} )

        #png2cpp( inputFile outFile )
        png2cpp( ${pngFile} ${cppFile} )
    endforeach()

endfunction()


if( MAINTAIN_PNGS )

    # Inkscape is required to convert SVG files to PNG files.
    set( Inkscape_FOUND FALSE )

    if( NOT Inkscape_FOUND )
        find_program( Inkscape_EXECUTABLE inkscape
                      PATHS "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Inkscape;InstallDir]"
                      DOC "Inkscape vector drawing program." )

        if( NOT Inkscape_EXECUTABLE )
            message( FATAL_ERROR "Could not find the Inkscape vector drawing program." )
        else( NOT Inkscape_EXECUTABLE )
            set( Inkscape_FOUND TRUE )
            set( Inkscape_EXECUTABLE ${Inkscape_EXECUTABLE}
                 CACHE FILEPATH "Path and file name of the Inkscape program." )
            message( STATUS "The Inkscape vector drawing program found." )
        endif( NOT Inkscape_EXECUTABLE )
    endif( NOT Inkscape_FOUND )

    # pngcrush is required to reduce the size of the converted PNG files.
    set( pngcrush_FOUND FALSE )

    if( NOT pngcrush_FOUND )
        find_program( pngcrush_EXECUTABLE pngcrush DOC "The PNG size reduction program." )

        if( NOT pngcrush_EXECUTABLE )
            message( FATAL_ERROR "Could not find the pngcrush PNG size reduction program." )
        else( NOT pngcrush_EXECUTABLE )
            set( pngcrush_FOUND TRUE )
            set( pngcrush_EXECUTABLE ${pngcrush_EXECUTABLE}
                 CACHE FILEPATH "Path and file name of the pngcrush program." )
            message( STATUS "The pngcrush PNG size reduction program found." )
        endif( NOT pngcrush_EXECUTABLE )
    endif( NOT pngcrush_FOUND )


    # these 3 cmake commands for each desired bitmap size set, repeat as needed:
    bitmap_dir( 16 16 "${BMAPS_SMALL}" )

    bitmap_dir( 26 26 "${BMAPS_MID}" )

    bitmap_dir( 48 48 "${BMAPS_BIG}" )

endif( MAINTAIN_PNGS )

    #message( "CPP_LIST: ${CPP_LIST}" )

    # a target with no output, just so changes get built
    #add_custom_target( make_cpp_files ALL DEPENDS ${CPP_LIST} )

set( CPP_LIST "" )

foreach( bmn ${BMAPS_SMALL} )
    #message( "library add cpp_16/${bmn}.cpp" )
    list( APPEND CPP_LIST cpp_16/${bmn}.cpp )
endforeach()

foreach( bmn ${BMAPS_MID} )
    #message( "library add cpp_26/${bmn}.cpp" )
    list( APPEND CPP_LIST cpp_26/${bmn}.cpp )
endforeach()

foreach( bmn ${BMAPS_BIG} )
    #message( "library add cpp_48/${bmn}.cpp" )
    list( APPEND CPP_LIST cpp_48/${bmn}.cpp )
endforeach()

foreach( bmn ${BMAPS_OTHER} )
    #message( "library add cpp_other/${bmn}.cpp" )
    list( APPEND CPP_LIST cpp_other/${bmn}.cpp )
endforeach()

#add_library( bitmaps SHARED ${CPP_LIST} )
add_library( bitmaps STATIC ${CPP_LIST} )
