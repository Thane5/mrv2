#pragma once

#ifndef __STDC_FORMAT_MACROS
#  define __STDC_FORMAT_MACROS
#  define __STDC_LIMIT_MACROS
#endif

#include <cinttypes>  // for PRId64
#include <memory>

#include <tlCore/Util.h>
#include <tlCore/BBox.h>

#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Scroll.H>

#include "mrvCore/mrvMedia.h"
#include "mrvCore/mrvString.h"

#include "mrvWidgets/mrvPopupMenu.h"
#include "mrvWidgets/mrvBrowser.h"
#include "mrvWidgets/mrvSlider.h"
#include "mrvWidgets/mrvTable.h"
#include "mrvWidgets/mrvCollapsibleGroup.h"

#include "mrvFl/mrvToolWidget.h"


class Fl_Box;
class Fl_Input;

class ViewerUI;

namespace mrv
{
    class Pack;
    class GLViewport;
    class TimelinePlayer;


    class ImageInfoTool : public ToolWidget
    {

    public:
        ImageInfoTool( ViewerUI* ui );
        ~ImageInfoTool();


        void refresh();


        TimelinePlayer* timelinePlayer() const;
        void setTimelinePlayer( TimelinePlayer* p );

        int line_height();
        void scroll_to( int w, int h ) {  /* @todo: */ };

        ViewerUI*    main() const;

        GLViewport*  view() const;

    protected:
        Fl_Color get_title_color();
        Fl_Color get_widget_color();

        void clear_callback_data();

        void hide_tabs();

        static void enum_cb( mrv::PopupMenu* w, ImageInfoTool* v );

        static void toggle_tab( Fl_Widget* w, void* data );
        static void int_slider_cb( Fl_Slider* w, void* data );
        static void float_slider_cb( Fl_Slider* w, void* data );

        double to_memory( long double value, const char*& extension );

        mrv::Table* add_browser( mrv::CollapsibleGroup* g );

        void add_button( const char* name, const char* tooltip,
                         Fl_Callback* callback = NULL,
                         Fl_Callback* callback2 = NULL );

        void add_scale( const char* name, const char* tooltip,
                        int pressed, int num_scales,
                        Fl_Callback* callback = NULL );

        void add_ocio_ics( const char* name, const char* tooltip,
                           const char* content,
                           const bool editable = true,
                           Fl_Callback* callback = NULL );

        void add_text( const char* name, const char* tooltip,
                       const char* content,
                       const bool editable = false,
                       const bool active = false,
                       Fl_Callback* callback = NULL );
        void add_text( const char* name, const char* tooltip,
                       const std::string& content,
                       const bool editable = false,
                       const bool active = false,
                       Fl_Callback* callback = NULL );
        void add_float( const char* name, const char* tooltip,
                        const float content,
                        const bool editable = false,
                        const bool active = false,
                        Fl_Callback* callback = NULL,
                        const float minV = 0.0f, const float maxV = 1.0f,
                        const int when = FL_WHEN_RELEASE,
                        const mrv::Slider::SliderType type =
                        mrv::Slider::kNORMAL );
        void add_rect( const char* name, const char* tooltip,
                       const tl::math::BBox2i& content,
                       const bool editable = false,
                       Fl_Callback* callback = NULL );

        void add_time( const char* name, const char* tooltip,
                       const double content,
                       const double fps, const bool editable = false );

        void add_enum( const char* name, const char* tooltip,
                       const size_t content,
                       const char* const* options,
                       const size_t num, const bool editable = false,
                       Fl_Callback* callback = NULL );

        void add_enum( const char* name, const char* tooltip,
                       const std::string& content,
                       stringArray& options, const bool editable = false,
                       Fl_Callback* callback = NULL );

        void add_int64( const char* name, const char* tooltip,
                        const int64_t content );

        void add_int( const char* name, const char* tooltip,
                      const int content,
                      const bool editable = false,
                      const bool active = true,
                      Fl_Callback* callback = NULL,
                      const int minV = 0, const int maxV = 10,
                      const int when = FL_WHEN_RELEASE );
        void add_int( const char* name, const char* tooltip,
                      const unsigned int content,
                      const bool editable = false,
                      const bool active = true,
                      Fl_Callback* callback = NULL,
                      const unsigned int minV = 0,
                      const unsigned int maxV = 9999 );
        void add_bool( const char* name, const char* tooltip,
                       const bool content,
                       const bool editable = false,
                       Fl_Callback* callback = NULL );

        void add_controls() override;
        void fill_data();


    public:
        CollapsibleGroup*       m_image;
        CollapsibleGroup*       m_video;
        CollapsibleGroup*       m_audio;
        CollapsibleGroup*       m_subtitle;
        CollapsibleGroup*       m_attributes;
        Fl_Input* m_entry;
        Fl_Choice*  m_type;

    protected:
        int           kMiddle;
        Table*             m_curr;
        Fl_Color          m_color;
        unsigned int group;
        unsigned int row;
        unsigned int X, Y, W, H;
        TimelinePlayer* player = nullptr;

    public:
        Fl_Menu_Button*   menu = nullptr;
    };

} // namespace mrv
