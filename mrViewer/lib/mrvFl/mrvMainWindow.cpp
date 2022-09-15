/*
    mrViewer - the professional movie and flipbook playback
    Copyright (C) 2007-2022  Gonzalo Garramuño

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file   mrvMainWindow.cpp
 * @author gga
 * @date   Wed Jul  4 23:17:46 2007
 *
 * @brief  main window for mrViewer
 *
 *
 */

#ifdef USE_R3DSDK
#  include "R3DSDK.h"
#endif

#include "mrvCore/mrvI8N.h"
#include "mrvCore/mrvHotkey.h"

#include "mrvFl/mrvIO.h"
#include "mrvFl/mrvCallbacks.h"
#include "mrvFl/mrvMainWindow.h"
#include "mrvPreferencesUI.h"

#include <FL/platform.H>
#include <FL/fl_utf8.h>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Enumerations.H>
#include <FL/Fl.H>
#include <FL/Fl_XPM_Image.H>


#if defined(FLTK_USE_X11)
#include <X11/extensions/scrnsaver.h>
#endif

#include "icons/viewer16.xpm"
#include "resource.h"



namespace {
    const char* kModule = "main";
}

namespace mrv {


MainWindow::MainWindow( int W, int H, const char* title ) :
Fl_Double_Window( W, H, title )
{
    xclass("mrViewer");
    set_icon();

    const char* r = fl_getenv( "MRV_ROOT" );
    if ( r )
    {
        Preferences::root = r;
    }

    if ( Preferences::root.empty() )
    {
        throw std::runtime_error("Environment variable MRV_ROOT not set.  "
                                 "Aborting");
    }

}

MainWindow::~MainWindow()
{
    // Restore screensaver/black screen
#if defined(FLTK_USE_X11)
    XScreenSaverSuspend( fl_display, False );
#elif defined(_WIN32)
    SetThreadExecutionState(ES_CONTINUOUS);
#elif defined(__APPLE__)
    if ( success )
    {
        success = IOPMAssertionRelease( assertionID );
    }
#endif

    // Close the R3D SDK
#ifdef USE_R3DSDK
    R3DSDK::FinalizeSdk();
#endif
}


void MainWindow::set_icon()
{
    fl_open_display();  // Needed for icons

    // Turn off screensaver and black screen
#if defined(FLTK_USE_X11)
    int event_base, error_base;
    Bool ok = XScreenSaverQueryExtension(fl_display, &event_base, &error_base );
    if ( ok == True )
        XScreenSaverSuspend( fl_display, True );
#elif defined(_WIN32)
    SetThreadExecutionState( ES_CONTINUOUS | ES_SYSTEM_REQUIRED |
                             ES_DISPLAY_REQUIRED );
#elif defined(__APPLE__)
    CFStringRef reason = CFSTR( "mrViewer playback" );
    success = IOPMAssertionCreateWithName( kIOPMAssertionTypeNoDisplaySleep,
                                           kIOPMAssertionLevelOn,
                                           reason, &assertionID );
#endif

#if defined(_WIN32)
     HICON data = LoadIcon(fl_display, MAKEINTRESOURCE(IDI_ICON1));
     this->icon(data);
#else
    Fl_Pixmap* pic = new Fl_Pixmap( viewer16_xpm );
    Fl_RGB_Image* rgb = new Fl_RGB_Image( pic );
    delete pic;
    icon( rgb );
    delete rgb;
#endif

}


#if !defined(__APPLE__)

    void MainWindow::always_on_top( int t )
    {
#if defined(_WIN32)
        HWND action;
        if ( t ) action = HWND_TOPMOST;
        else     action = HWND_NOTOPMOST;
        // Microsoft (R) Windows(TM)
        SetWindowPos(fl_xid(this), action,
                     NULL, NULL, NULL, NULL, SWP_NOMOVE | SWP_NOSIZE );
#elif defined(FLTK_USE_X11)
        // XOrg / XWindows(TM)
        XEvent ev;
        static const char* const names[2] = { "_NET_WM_STATE",
            "_NET_WM_STATE_ABOVE"
        };
        Atom atoms[ 2 ];
        fl_open_display();
        XInternAtoms(fl_display, (char**)names, 2, False, atoms );
        Atom net_wm_state = atoms[ 0 ];
        Atom net_wm_state_above = atoms[ 1 ];
        ev.type = ClientMessage;
        ev.xclient.window = fl_xid(this);
        ev.xclient.message_type = net_wm_state;
        ev.xclient.format = 32;
        ev.xclient.data.l[ 0 ] = t;
        ev.xclient.data.l[ 1 ] = net_wm_state_above;
        ev.xclient.data.l[ 2 ] = 0;
        XSendEvent(fl_display,
                   DefaultRootWindow(fl_display),  False,
                   SubstructureNotifyMask|SubstructureRedirectMask, &ev);
#endif
    } // above_all function


#endif


    void MainWindow::iconize_all()
    {
        // @todo:
        // Fl_Window* uiReelWindow = ui->uiReelWindow->uiMain;
        // if (uiReelWindow) uiReelWindow->iconize();
        return Fl_Double_Window::iconize();
    }


    void MainWindow::fill_menu( Fl_Menu_* menu )
    {
        Fl_Menu_Item* item = nullptr;
        menu->clear();

        int idx = 1;

        menu->add( _("File/Open/Movie or Sequence"),
                   kOpenImage.hotkey(),
                   (Fl_Callback*)open_cb, ui );

#if 0

        menu->add( _("File/Open/Single Image"), kOpenSingleImage.hotkey(),
                   (Fl_Callback*)open_single_cb, ui );
        menu->add( _("File/Open/Directory"), kOpenDirectory.hotkey(),
                   (Fl_Callback*)open_dir_cb, ui );
        idx = menu->add( _("File/Open/Session"),
                         kOpenSession.hotkey(),
                         (Fl_Callback*)open_session_cb, ui );

        bool hasMedia = false;  // @todo: get list of loaded images
        if ( hasMedia )
        {
            // menu->add( _("File/Open/Stereo Sequence or Movie"),
            //            kOpenStereoImage.hotkey(),
            //            (Fl_Callback*)open_stereo_cb, ui );
            menu->add( _("File/Save/Movie or Sequence As"),
                       kSaveSequence.hotkey(),
                       (Fl_Callback*)save_sequence_cb, ui->uiView );
            menu->add( _("File/Save/OTIO As"), kSaveReel.hotkey(),
                       (Fl_Callback*)save_otio_cb, ui->uiView );
            menu->add( _("File/Save/Frame As"), kSaveImage.hotkey(),
                       (Fl_Callback*)save_cb, ui->uiView );
            menu->add( _("File/Save/GL Snapshots As"), kSaveSnapshot.hotkey(),
                       (Fl_Callback*)save_snap_cb, ui->uiView );
            menu->add( _("File/Save/Session As"),
                       kSaveSession.hotkey(),
                       (Fl_Callback*)save_session_as_cb, ui->uiView );
            idx += 2;
        }

        Fl_Menu_Item* item = (Fl_Menu_Item*) &menu->menu()[idx];

        if ( dynamic_cast< Fl_Menu_Bar* >( menu ) )
        {
            item->flags |= FL_MENU_DIVIDER;
            menu->add( _("File/Quit"), kQuitProgram.hotkey(),
                       (Fl_Callback*)exit_cb, ui );
        }


        char buf[256];
        int num = ui->uiWindows->children() - 1;
        int i;
        for ( i = 0; i < num; ++i )
        {
            std::string tmp = ui->uiWindows->child(i)->label();

            // Quote any / to avoid submenu ( A/B Options for example ).
            size_t pos = tmp.find( '/' );
            if ( pos != std::string::npos )
            {
                tmp = tmp.substr( 0, pos ) + '\\' +
                      tmp.substr( pos, tmp.size() );
            }
            unsigned hotkey = 0;
            if ( tmp == _("Reels") ) hotkey = kToggleReel.hotkey();
            else if ( tmp == _("Media Info") ) hotkey = kToggleMediaInfo.hotkey();
            else if ( tmp == _("Color Info") ) hotkey = kToggleColorInfo.hotkey();
            else if ( tmp == _("Color Controls") )
                hotkey = kToggleColorControls.hotkey();
            else if ( tmp == _("Action Tools") ) hotkey = kToggleAction.hotkey();
            else if ( tmp == _("A-B, Stereo 3D Options") )
                hotkey = kToggleStereoOptions.hotkey();
            else if ( tmp == _("Preferences") )
                hotkey = kTogglePreferences.hotkey();
            else if ( tmp == _("EDL Edit") )
                hotkey = kToggleEDLEdit.hotkey();
            else if ( tmp == _("3dView") )
                hotkey = kToggle3dView.hotkey();
            else if ( tmp == _("Histogram") )
                hotkey = kToggleHistogram.hotkey();
            else if ( tmp == _("Vectorscope") )
                hotkey = kToggleVectorscope.hotkey();
            else if ( tmp == _("Waveform") )
                hotkey = kToggleWaveform.hotkey();
            else if ( tmp == _("ICC Profiles") )
                hotkey = kToggleICCProfiles.hotkey();
            else if ( tmp == _("Connections") )
                hotkey = kToggleConnections.hotkey();
            else if ( tmp == _("Hotkeys") )
                hotkey = kToggleHotkeys.hotkey();
            else if ( tmp == _("Logs") )
                hotkey = kToggleLogs.hotkey();
            else if ( tmp == _("About") )
                hotkey = kToggleAbout.hotkey();
            else
                continue; // Unknown window check
            tmp = _("Windows/") + tmp;
            menu->add( tmp.c_str(), hotkey, (Fl_Callback*)window_cb, ui );
        }
#endif


#if 0
        if ( hasMedia )
        {

            menu->add( _("View/Safe Areas"), kSafeAreas.hotkey(),
                       (Fl_Callback*)safe_areas_cb, ui->uiView );

            idx = menu->add( _("View/Display Window"), kDisplayWindow.hotkey(),
                             (Fl_Callback*)display_window_cb, ui->uiView,
                             FL_MENU_TOGGLE );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( display_window() ) item->set();

            idx = menu->add( _("View/Data Window"), kDataWindow.hotkey(),
                             (Fl_Callback*)data_window_cb, ui->uiView, FL_MENU_TOGGLE );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( data_window() ) item->set();
#endif


            idx = menu->add( _("Render/Red Channel"), kRedChannel.hotkey(),
                             (Fl_Callback*)toggle_red_channel_cb, ui->uiView,
                             FL_MENU_RADIO );
            idx = menu->add( _("Render/Green Channel"), kGreenChannel.hotkey(),
                             (Fl_Callback*)toggle_green_channel_cb, ui->uiView,
                             FL_MENU_RADIO );
            idx = menu->add( _("Render/Blue Channel"),  kBlueChannel.hotkey(),
                             (Fl_Callback*)toggle_blue_channel_cb, ui->uiView,
                             FL_MENU_RADIO );
            idx = menu->add( _("Render/Alpha Channel"), kAlphaChannel.hotkey(),
                             (Fl_Callback*)toggle_alpha_channel_cb, ui->uiView,
                             FL_MENU_DIVIDER | FL_MENU_RADIO );

            idx = menu->add( _("Render/Mirror X"),
                             kFlipX.hotkey(), (Fl_Callback*)mirror_x_cb,
                             ui->uiView );

            idx = menu->add( _("Render/Mirror Y"),
                             kFlipY.hotkey(), (Fl_Callback*)mirror_y_cb,
                             ui->uiView, FL_MENU_DIVIDER );


            idx = menu->add( _("Render/Minify Filter/Nearest"),
                             0, (Fl_Callback*)display_options_cb, ui->uiView,
                             FL_MENU_RADIO );



            const timeline::DisplayOptions& o = ui->uiView->getDisplayOptions();

            idx = menu->add( _("Render/Minify Filter/Nearest"),
                             0, (Fl_Callback*)display_options_cb, ui->uiView,
                             FL_MENU_RADIO );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( o.imageFilters.minify == timeline::ImageFilter::Nearest )
                item->set();

            idx = menu->add( _("Render/Minify Filter/Linear"),
                             0, (Fl_Callback*)display_options_cb, ui->uiView,
                             FL_MENU_RADIO );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( o.imageFilters.minify == timeline::ImageFilter::Linear )
                item->set();

            idx = menu->add( _("Render/Magnify Filter/Nearest"),
                             0, (Fl_Callback*)display_options_cb, ui->uiView,
                             FL_MENU_RADIO );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( o.imageFilters.magnify == timeline::ImageFilter::Nearest )
                item->set();

            idx = menu->add( _("Render/Magnify Filter/Linear"),
                             kTextureFiltering.hotkey(),
                             (Fl_Callback*)display_options_cb, ui->uiView,
                             FL_MENU_RADIO );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( o.imageFilters.magnify == timeline::ImageFilter::Linear )
                item->set();

#if 0

            sprintf( buf, "%s", _("View/Toggle Action Dock") );
            idx = menu->add( buf, kToggleToolBar.hotkey(),
                             (Fl_Callback*)toggle_action_tool_dock, ui,
                             FL_MENU_TOGGLE );
            item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            if ( ui->uiToolsGroup->visible() )
                item->set();

            const char* tmp;
            num = ui->uiPrefs->uiPrefsCropArea->children();
            for ( i = 0; i < num; ++i )
            {
                tmp = ui->uiPrefs->uiPrefsCropArea->child(i)->label();
                if ( !tmp ) continue;
                sprintf( buf, _("View/Mask/%s"), tmp );
                idx = menu->add( buf, 0, (Fl_Callback*)masking_cb, ui,
                                 FL_MENU_RADIO );
                item = (Fl_Menu_Item*) &(menu->menu()[idx]);
                float mask = kCrops[i];
                if ( mrv::is_equal( mask, _masking ) ) item->set();
            }


            sprintf( buf, "%s", _("View/Grid/Toggle Selected") );
            menu->add( buf, kGridToggle.hotkey(),
                       (Fl_Callback*)grid_toggle_cb, ui->uiView );

            sprintf( buf, "%s", _("View/Grid/Size") );
            menu->add( buf, kGridSize.hotkey(),
                       (Fl_Callback*)grid_size_cb, ui->uiView );

            sprintf( buf, "%s", _("View/Hud/Toggle Selected") );
            menu->add( buf, kHudToggle.hotkey(),
                       (Fl_Callback*)hud_toggle_cb, ui );

            num = ui->uiPrefs->uiPrefsHud->children();
            for ( i = 0; i < num; ++i )
            {
                tmp = ui->uiPrefs->uiPrefsHud->child(i)->label();
                sprintf( buf, _("View/Hud/%s"), tmp );
                idx = menu->add( buf, 0, (Fl_Callback*)hud_cb, ui,
                                 FL_MENU_TOGGLE );
                item = (Fl_Menu_Item*) &(menu->menu()[idx]);
                if ( hud() & (1 << i) ) item->set();
            }


            bool has_version = false;

            size_t pos = 0;

            PreferencesUI* prefs = main()->uiPrefs;
            std::string prefix = prefs->uiPrefsImageVersionPrefix->value();
            if ( (pos = file.find( prefix, pos) ) != std::string::npos )
                has_version = true;

            if ( has_version )
            {
                menu->add( _("Version/First"), kFirstVersionImage.hotkey(),
                           (Fl_Callback*)first_image_version_cb, ui,
                           FL_MENU_DIVIDER);
                menu->add( _("Version/Next"), kNextVersionImage.hotkey(),
                           (Fl_Callback*)next_image_version_cb, ui);
                menu->add( _("Version/Previous"),
                           kPreviousVersionImage.hotkey(),
                           (Fl_Callback*)previous_image_version_cb,
                           ui, FL_MENU_DIVIDER);
                menu->add( _("Version/Last"),
                           kLastVersionImage.hotkey(),
                           (Fl_Callback*)last_image_version_cb,
                           ui, FL_MENU_DIVIDER);
            }


            menu->add( _("Image/Next"), kNextImage.hotkey(),
                       (Fl_Callback*)next_image_cb, ui);
            menu->add( _("Image/Previous"), kPreviousImage.hotkey(),
                       (Fl_Callback*)previous_image_cb,
                       ui, FL_MENU_DIVIDER);

            menu->add( _("Image/Update Single Frame in Cache"),
                       kClearSingleFrameCache.hotkey(),
                       (Fl_Callback*)update_frame_cb, ui->uiView,
                       FL_MENU_DIVIDER );



            // menu->add( _("Image/Rotate +90"),
            //            kRotatePlus90.hotkey(),
            //            (Fl_Callback*)rotate_plus_90_cb, ui->uiView );
            // menu->add( _("Image/Rotate -90"),
            //            kRotateMinus90.hotkey(),
            //            (Fl_Callback*)rotate_minus_90_cb, ui->uiView,
            //            FL_MENU_DIVIDER );


            menu->add( _("Image/Mirror/Horizontal"),
                       kFlipX.hotkey(),
                       (Fl_Callback*)flip_x_cb,
                       ui->uiView);
            menu->add( _("Image/Mirror/Vertical"),
                       kFlipY.hotkey(),
                       (Fl_Callback*)flip_y_cb,
                       ui->uiView);
            menu->add( _("Image/Set as Background"), kSetAsBG.hotkey(),
                       (Fl_Callback*)set_as_background_cb,
                       (void*)ui->uiView);


            // menu->add( _("Image/Switch HASMEDIA and BG"),
            //            kSwitchHASMEDIABG.hotkey(),
            //            (Fl_Callback*)switch_hasMedia_bg_cb, (void*)ui->uiView);
            // menu->add( _("Image/Toggle Background"),
            //            kToggleBG.hotkey(),
            //            (Fl_Callback*)toggle_background_cb, (void*)ui->uiView);


            // mrv::ImageBrowser* b = ui;
            // mrv::Reel reel = b->current_reel();
            // if ( reel->images.size() > 1 )
            // {
            //     menu->add( _("Image/Toggle EDL"),
            //                kToggleEDL.hotkey(),
            //                (Fl_Callback*)toggle_edl_cb, (void*)ui->uiView);
            // }



            if ( Preferences::use_ocio )
            {


                menu->add( _("OCIO/Input Color Space"),
                           kOCIOInputColorSpace.hotkey(),
                           (Fl_Callback*)attach_ocio_ics_cb, (void*)ui);

                menu->add( _("OCIO/Display"),
                           kOCIODisplay.hotkey(),
                           (Fl_Callback*)attach_ocio_display_cb, (void*)ui->uiView);
                menu->add( _("OCIO/View"),
                           kOCIOView.hotkey(),
                           (Fl_Callback*)attach_ocio_view_cb, (void*)ui->uiView);
            }




            // size_t num = image->number_of_video_streams();
            // if ( num > 1 )
            // {


            //     for ( unsigned i = 0; i < num; ++i )
            //     {
            //         char buf[256];
            //         sprintf( buf, _("Video/Track #%d - %s"), i,
            //                  image->video_info(i).language.c_str() );

            //         idx = menu->add( buf, 0,
            //                          (Fl_Callback*)change_video_cb, ui->uiView,
            //                          FL_MENU_RADIO );
            //         item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            //         if ( image->video_stream() == (int) i )
            //             item->set();
            //     }
            // }

            // num = image->number_of_subtitle_streams();

            // if ( dynamic_cast< aviImage* >( image ) != NULL )
            // {
            //     menu->add( _("Subtitle/Load"), 0,
            //                (Fl_Callback*)load_subtitle_cb, ui );
            // }



            // if ( num > 0 )
            // {
            //     idx = menu->add( _("Subtitle/No Subtitle"), 0,
            //                      (Fl_Callback*)change_subtitle_cb, ui->uiView,
            //                      FL_MENU_TOGGLE  );
            //     Fl_Menu_Item* item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            //     if ( image->subtitle_stream() == -1 )
            //         item->set();
            //     for ( unsigned i = 0; i < num; ++i )
            //     {
            //         char buf[256];
            //         sprintf( buf, _("Subtitle/Track #%d - %s"), i,
            //                  image->subtitle_info(i).language.c_str() );

            //         idx = menu->add( buf, 0,
            //                          (Fl_Callback*)change_subtitle_cb, ui->uiView, FL_MENU_RADIO );
            //         item = (Fl_Menu_Item*) &(menu->menu()[idx]);
            //         if ( image->subtitle_stream() == (int)i )
            //             item->set();
            //     }
            // }



            // if ( 1 )
            // {

            //     menu->add( _("Audio/Attach Audio File"), kAttachAudio.hotkey(),
            //                (Fl_Callback*)attach_audio_cb, ui->uiView );
            //     menu->add( _("Audio/Edit Audio Frame Offset"),
            //                kEditAudio.hotkey(),
            //                (Fl_Callback*)edit_audio_cb, ui->uiView );
            //     menu->add( _("Audio/Detach Audio File"), kDetachAudio.hotkey(),
            //                (Fl_Callback*)detach_audio_cb, ui->uiView );
            // }



            // if ( dynamic_cast< Fl_Menu_Button* >( menu ) )
            // {
            //     menu->add( _("Pixel/Copy RGBA Values to Clipboard"),
            //                kCopyRGBAValues.hotkey(),
            //                (Fl_Callback*)copy_pixel_rgba_cb, (void*)ui->uiView);
            // }



        }
#endif



        menu->menu_end();
        Fl_Sys_Menu_Bar* smenubar = dynamic_cast< Fl_Sys_Menu_Bar* >( menu );
        if ( smenubar )
        {
            smenubar->update();
        }

        menu->redraw();


    }

} // namespace mrv
