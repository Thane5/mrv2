// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2023 Darby Johnston
// All rights reserved.

#include <memory>

#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl.H>
#include <FL/names.h>

#include <tlCore/StringFormat.h>

#include <tlIO/Cache.h>

#include <tlTimelineUI/TimelineWidget.h>

#include <tlGL/GL.h>
#include <tlGL/GLFWWindow.h>

#include <tlUI/IClipboard.h>
#include <tlUI/IWindow.h>
#include <tlUI/RowLayout.h>

#include <tlTimelineGL/Render.h>

#include <tlGL/Init.h>
#include <tlGL/Mesh.h>
#include <tlGL/OffscreenBuffer.h>
#include <tlGL/Shader.h>

#include "mrvCore/mrvFile.h"
#include "mrvCore/mrvImage.h"
#include "mrvCore/mrvHotkey.h"
#include "mrvCore/mrvTimeObject.h"

#include "mrvEdit/mrvEditCallbacks.h"
#include "mrvEdit/mrvEditUtil.h"

#include "mrvFl/mrvIO.h"

#include "mrvUI/mrvDesktop.h"

#include "mrvGL/mrvTimelineWidget.h"
#include "mrvGL/mrvGLErrors.h"

#include "mrvNetwork/mrvTCP.h"

#include "mrvApp/mrvSettingsObject.h"

#include "mrViewer.h"

#include <FL/platform.H>

namespace mrv
{
    namespace
    {
        const int kTHUMB_WIDTH = 128;
        const int kTHUMB_HEIGHT = 80;

        const double kTimeout = 0.008; // 120 fps
        const char* kModule = "timelineui";
    } // namespace

    namespace
    {
        
        int getIndex(const otio::SerializableObject::Retainer<otio::Composable>&
                     composable)
        {
            int out = -1;
            if (composable && composable->parent())
            {
                const auto& children = composable->parent()->children();
                for (int i = 0; i < children.size(); ++i)
                {
                    if (composable == children[i].value)
                    {
                        out = i;
                        break;
                    }
                }
            }
            return out;
        }
    } // namespace

    namespace
    {
        // These classes (TimelineWindow and Clipboard) are needed to act
        // from FLTK to Darby's UI translations.
        class TimelineWindow : public ui::IWindow
        {
            TLRENDER_NON_COPYABLE(TimelineWindow);

        public:
            void _init(const std::shared_ptr<system::Context>& context)
            {
                IWindow::_init(
                    "tl::anonymous::TimelineWindow", context, nullptr);
            }

            TimelineWindow() {}

        public:
            virtual ~TimelineWindow() {}

            static std::shared_ptr<TimelineWindow>
            create(const std::shared_ptr<system::Context>& context)
            {
                auto out = std::shared_ptr<TimelineWindow>(new TimelineWindow);
                out->_init(context);
                return out;
            }

            bool key(ui::Key key, bool press, int modifiers)
            {
                return _key(key, press, modifiers);
            }

            void text(const std::string& text) { _text(text); }

            void cursorEnter(bool enter) { _cursorEnter(enter); }

            void cursorPos(const math::Vector2i& value) { _cursorPos(value); }

            void mouseButton(int button, bool press, int modifiers)
            {
                _mouseButton(button, press, modifiers);
            }

            void scroll(const math::Vector2f& value, int modifiers)
            {
                _scroll(value, modifiers);
            }

            void setGeometry(const math::Box2i& value) override
            {
                IWindow::setGeometry(value);
                for (const auto& i : _children)
                {
                    i->setGeometry(value);
                }
            }
        };

        class Clipboard : public ui::IClipboard
        {
            TLRENDER_NON_COPYABLE(Clipboard);

        public:
            void _init(const std::shared_ptr<system::Context>& context)
            {
                IClipboard::_init(context);
            }

            Clipboard() {}

        public:
            ~Clipboard() override {}

            static std::shared_ptr<Clipboard>
            create(const std::shared_ptr<system::Context>& context)
            {
                auto out = std::shared_ptr<Clipboard>(new Clipboard);
                out->_init(context);
                return out;
            }

            std::string getText() const override
            {
                std::string text;
                if (Fl::event_text())
                    text = Fl::event_text();
                return text;
            }

            void setText(const std::string& value) override
            {
                Fl::copy(value.c_str(), value.size());
            }
        };
    } // namespace

    struct TimelineWidget::Private
    {
        std::weak_ptr<system::Context> context;

        ViewerUI* ui = nullptr;
        Fl_Window* topWindow = nullptr;

        TimelinePlayer* player = nullptr;

        // New thumbnail variables
        int thumbnailWidth = kTHUMB_WIDTH;
        std::vector<tl::file::MemoryRead> memoryRead;
        std::shared_ptr<ui::ThumbnailGenerator> thumbnailGenerator;
        std::shared_ptr<gl::GLFWWindow> window;
        std::map<std::string, std::shared_ptr<image::Image> > thumbnails;

        // Requests classes
        io::Options ioOptions;
        ui::InfoRequest infoRequest;
        std::shared_ptr<io::Info> ioInfo;
        std::map<otime::RationalTime, ui::ThumbnailRequest> thumbnailRequests;
        tl::file::Path path;
        
        // FLTK classes
        Fl_Double_Window* thumbnailWindow = nullptr; // thumbnail window
        Fl_Box* box = nullptr;
        
        mrv::TimeUnits units = mrv::TimeUnits::Timecode;

        std::shared_ptr<ui::Style> style;
        std::shared_ptr<ui::IconLibrary> iconLibrary;
        std::shared_ptr<image::FontSystem> fontSystem;
        std::shared_ptr<Clipboard> clipboard;
        std::shared_ptr<timeline::IRender> render;
        timelineui::ItemOptions itemOptions;
        std::shared_ptr<timelineui::TimelineWidget> timelineWidget;
        std::shared_ptr<TimelineWindow> timelineWindow;
        std::shared_ptr<tl::gl::Shader> shader;
        std::shared_ptr<tl::gl::OffscreenBuffer> buffer;
        std::shared_ptr<gl::VBO> vbo;
        std::shared_ptr<gl::VAO> vao;
        std::chrono::steady_clock::time_point mouseWheelTimer;

        //! Flags
        bool draggingClip = false;
        bool continueReversePlaying = false;

        //! Observers
        std::shared_ptr<observer::ValueObserver<timeline::PlayerCacheInfo> >
            cacheInfoObserver;

        std::vector<otime::RationalTime> annotationTimes;
        otime::TimeRange timeRange = time::invalidTimeRange;
    };

    TimelineWidget::TimelineWidget(int X, int Y, int W, int H, const char* L) :
        Fl_SuperClass(X, Y, W, H, L),
        _p(new Private)
    {
        int fl_double = FL_DOUBLE; // _WIN32 needs this

        // Do not use FL_DOUBLE on APPLE as it makes playback slow
#if defined(__APPLE__) || defined(__linux__)
        fl_double = 0;
        if (desktop::Wayland())
        {
            // For faster playback, we won't set this window to FL_DOUBLE.
            // FLTK's EGL Wayland already uses two buffers.
            // fl_double = FL_DOUBLE;
        }
        else if (desktop::XWayland())
        {
            fl_double = FL_DOUBLE;
        }
#endif
        mode(FL_RGB | FL_ALPHA | FL_STENCIL | fl_double | FL_OPENGL3);
    }

    void TimelineWidget::setContext(
        const std::shared_ptr<system::Context>& context,
        const std::shared_ptr<timeline::TimeUnitsModel>& timeUnitsModel,
        ViewerUI* ui)
    {
        TLRENDER_P();

        p.context = context;

        p.ui = ui;
        p.topWindow = ui->uiMain;

        auto settings = ui->app->settings();

        p.style = ui::Style::create(context);
        p.iconLibrary = ui::IconLibrary::create(context);
        p.fontSystem = image::FontSystem::create(context);
        p.clipboard = Clipboard::create(context);

        p.timelineWidget =
            timelineui::TimelineWidget::create(timeUnitsModel, context);
        p.timelineWidget->setEditable(false);
        p.timelineWidget->setFrameView(true);
        p.timelineWidget->setScrollBarsVisible(false);
        p.timelineWidget->setMoveCallback(std::bind(
            &mrv::TimelineWidget::moveCallback, this, std::placeholders::_1));

        timelineui::ItemOptions itemOptions;
        itemOptions.trackInfo = settings->getValue<bool>("Timeline/TrackInfo");
        itemOptions.clipInfo = settings->getValue<bool>("Timeline/ClipInfo");
        p.timelineWidget->setItemOptions(itemOptions);

        p.timelineWindow = TimelineWindow::create(context);
        p.timelineWindow->setClipboard(p.clipboard);
        p.timelineWidget->setParent(p.timelineWindow);

        p.window = gl::GLFWWindow::create(
            "mrv::TimelineWidget::window",
            math::Size2i(1, 1),
            context,
            static_cast<int>(gl::GLFWWindowOptions::None));
            
        p.thumbnailGenerator = ui::ThumbnailGenerator::create(context,
                                                              p.window);
        
        setStopOnScrub(false);

        _styleUpdate();

        Fl::add_timeout(kTimeout, (Fl_Timeout_Handler)timerEvent_cb, this);
    }

    std::shared_ptr<ui::ThumbnailGenerator>
    TimelineWidget::thumbnailGenerator() const
    {
        return _p->thumbnailGenerator;
    }

    void TimelineWidget::setStyle(const std::shared_ptr<ui::Style>& style)
    {
        _p->style = style;
        _styleUpdate();
    }

    void TimelineWidget::hideThumbnail()
    {
        TLRENDER_P();
        if (!p.thumbnailWindow)
            return;
        p.thumbnailWindow->hide();
    }

    TimelineWidget::~TimelineWidget()
    {
    }

    bool TimelineWidget::isEditable() const
    {
        return _p->timelineWidget->isEditable();
    }

    void TimelineWidget::setEditable(bool value)
    {
        _p->timelineWidget->setEditable(value);
    }

    void TimelineWidget::setScrollBarsVisible(bool value)
    {
        _p->timelineWidget->setScrollBarsVisible(value);
    }

    void TimelineWidget::setScrollToCurrentFrame(bool value)
    {
        _p->timelineWidget->setScrollToCurrentFrame(value);
    }

    static void continue_playing_cb(TimelineWidget* t)
    {
        t->continuePlaying();
    }

    void TimelineWidget::continuePlaying()
    {
        TLRENDER_P();

        p.continueReversePlaying = true;

        //
        // Thie observer will watch the cache and start a reverse playback
        // once it is filled.
        //
        p.cacheInfoObserver =
            observer::ValueObserver<timeline::PlayerCacheInfo>::create(
                p.player->player()->observeCacheInfo(),
                [this](const timeline::PlayerCacheInfo& value)
                {
                    TLRENDER_P();
                    if (p.player->playback() != timeline::Playback::Stop &&
                        p.continueReversePlaying)
                        return;

                    const auto& cache =
                        p.player->player()->observeCacheOptions()->get();
                    const auto& readAhead = cache.readAhead;
                    const auto& readBehind = cache.readBehind;
                    const auto& endTime = p.player->currentTime() + readBehind;
                    const auto& startTime = endTime - readAhead;

                    bool found = false;
                    for (const auto& t : value.videoFrames)
                    {
                        if (t.start_time() <= startTime &&
                            t.end_time_exclusive() >= endTime)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (found)
                    {
                        p.ui->uiView->setPlayback(timeline::Playback::Reverse);
                        p.continueReversePlaying = false;
                    }
                },
                observer::CallbackAction::Suppress);
    }

    int TimelineWidget::_seek()
    {
        TLRENDER_P();
        const int maxY = 48;
        const int Y = _toUI(Fl::event_y());
        const int X = _toUI(Fl::event_x());
        if ((Y < maxY && !p.timelineWidget->isDraggingClip()) ||
            !p.timelineWidget->isEditable())
        {
            p.timeRange = p.player->player()->getTimeRange();
            const auto& info = p.player->ioInfo();
            const auto& time = _posToTime(X);
            p.player->seek(time);
            // \@note: Jumping frames when playin in reverse on 4K movies can
            //         lead to seeking issues in tlRender when the images are
            //         not in cache.
            //         We stop the playbay and set an FLTK timeout to watch
            //         on the cache until it is filled and we continue
            //         playing from there.
            //
            if (file::isMovie(p.path) &&
                p.player->playback() == timeline::Playback::Reverse &&
                !info.video.empty() && info.video[0].size.w > 2048)
            {
                p.player->stop();
                Fl::add_timeout(
                    0.005, (Fl_Timeout_Handler)continue_playing_cb, this);
            }
            return 1;
        }
        else
        {
            p.draggingClip = p.timelineWidget->isDraggingClip();
            return 0;
        }
    }

    void TimelineWidget::_createThumbnailWindow()
    {
        TLRENDER_P();

        int X, Y;
        _getThumbnailPosition(X, Y);

        // Open a thumbnail window just above the timeline
        Fl_Group::current(p.topWindow);
        p.thumbnailWindow =
            new Fl_Double_Window(X, Y, kTHUMB_WIDTH, kTHUMB_HEIGHT);
        p.thumbnailWindow->box(FL_FLAT_BOX);
        p.thumbnailWindow->color(0xffffffff);
        p.thumbnailWindow->clear_border();
        p.thumbnailWindow->begin();

        p.box = new Fl_Box(2, 2, kTHUMB_WIDTH - 4, kTHUMB_HEIGHT - 4);
        p.box->box(FL_FLAT_BOX);
        p.box->labelcolor(fl_contrast(p.box->labelcolor(), p.box->color()));
        p.thumbnailWindow->end();
        p.thumbnailWindow->resizable(0);
        p.thumbnailWindow->show();
        Fl_Group::current(nullptr);
    }

    void TimelineWidget::_getThumbnailPosition(int& X, int& Y)
    {
        TLRENDER_P();
        X = Fl::event_x_root() - p.topWindow->x_root() -
            (p.thumbnailWidth + 4) / 2;
        if (X < 0)
            X = 0;

        int maxW = p.topWindow->w() - p.thumbnailWidth - 4;
        if (X > maxW)
            X = maxW;

        // 20 here is the size of the timeline without the pictures
        Y = y_root() - p.topWindow->y_root() - 20 - kTHUMB_HEIGHT;
    }

    void TimelineWidget::repositionThumbnail()
    {
        TLRENDER_P();
        if (Fl::belowmouse() == this)
        {
            int X, Y;
            _getThumbnailPosition(X, Y);
            p.thumbnailWindow->resize(X, Y, p.thumbnailWidth + 4,
                                      kTHUMB_HEIGHT);
            p.box->resize(2, 2, p.thumbnailWidth, kTHUMB_HEIGHT - 4);
            p.thumbnailWindow->show(); // needed for Windows
        }
        else
        {
            hideThumbnail();
        }
    }

    void
    TimelineWidget::_updateThumbnail(const std::shared_ptr<image::Image>& image)
    {
        TLRENDER_P();
        
        const auto W = image->getWidth();
        const auto H = image->getHeight();
        const int bytes = image->getDataByteCount();
        const int depth = bytes / W / H;
        const uint8_t* data = image->getData();
        const auto rgbImage = new Fl_RGB_Image(data, W, H, depth);

        Fl_Image* boxImage = p.box->image();
        p.box->bind_image(rgbImage);
        p.box->redraw();

        if (p.thumbnailWidth != W)
        {
            p.thumbnailWidth = W;
            repositionThumbnail();
        }
    }
    
    int TimelineWidget::requestThumbnail(bool fetch)
    {
        TLRENDER_P();

        if (!p.player || !p.ui->uiPrefs->uiPrefsTimelineThumbnails->value())
        {
            hideThumbnail();
            return 0;
        }

        const auto player = p.player->player();
        p.timeRange = player->getTimeRange();

        char buffer[64];
        if (!p.thumbnailWindow)
        {
            _createThumbnailWindow();
        }

        repositionThumbnail();

        const auto& time = _posToTime(_toUI(Fl::event_x()));
        uint16_t layerId = p.ui->uiColorChannel->value();
        timeToText(buffer, time, _p->units);
        p.box->copy_label(buffer);
        
        _cancelRequests();
        
        if (!p.ioInfo && !p.infoRequest.future.valid())
        {
            p.infoRequest = p.thumbnailGenerator->getInfo(p.path, p.memoryRead);
        }

        if (!fetch) return 1;
                
        p.ioOptions["OpenEXR/IgnoreDisplayWindow"] =
            string::Format("{0}").arg(
                App::ui->uiView->getIgnoreDisplayWindow());
        p.ioOptions["Layer"] =
            string::Format("{0}").arg(layerId);
        // @todo: p.ioOptions["USD/cameraName"] = p.clipName;
        const auto i = p.thumbnails.find(
            io::getCacheKey(p.path, time, p.ioOptions));
        if (i != p.thumbnails.end())
        {
            _updateThumbnail(i->second);
        }
        else if (p.ioInfo && !p.ioInfo->video.empty())
        {
            const auto k = p.thumbnailRequests.find(time);
            if (k == p.thumbnailRequests.end())
            {
                p.thumbnailRequests[time] =
                    p.thumbnailGenerator->getThumbnail(
                        p.path, p.memoryRead, p.box->h() - 24,
                        time, p.ioOptions);
            }
        }
        return 1;
    }

    //! Get timelineUI's timelineWidget item options
    timelineui::ItemOptions TimelineWidget::getItemOptions() const
    {
        return _p->timelineWidget->getItemOptions();
    }

    void TimelineWidget::setTimelinePlayer(TimelinePlayer* player)
    {
        TLRENDER_P();
        if (player == p.player)
            return;
        p.player = player;
        if (player)
        {
            auto innerPlayer = player->player();
            p.timeRange = innerPlayer->getTimeRange();
            p.timelineWidget->setPlayer(innerPlayer);
            
            auto model = p.ui->app->filesModel();
            auto Aitem = model->observeA()->get();
            if (Aitem)
                p.path = Aitem->path;
            else
                p.path = player->path();
        }
        else
        {
            _cancelRequests();
            p.box->bind_image(nullptr);
            
            p.timeRange = time::invalidTimeRange;
            p.timelineWidget->setPlayer(nullptr);
        }
    }

    bool TimelineWidget::hasFrameView() const
    {
        return _p->timelineWidget->hasFrameView();
    }

    void TimelineWidget::setFrameView(bool value)
    {
        _p->timelineWidget->setFrameView(value);
    }

    void TimelineWidget::setScrollKeyModifier(ui::KeyModifier value)
    {
        _p->timelineWidget->setScrollKeyModifier(value);
    }

    void TimelineWidget::setStopOnScrub(bool value)
    {
        _p->timelineWidget->setStopOnScrub(value);
    }

    void TimelineWidget::setThumbnails(bool value)
    {
        TLRENDER_P();
        p.itemOptions.thumbnails = value;
        _p->timelineWidget->setItemOptions(p.itemOptions);
    }

    void TimelineWidget::setMouseWheelScale(float value)
    {
        _p->timelineWidget->setMouseWheelScale(value);
    }

    void TimelineWidget::setItemOptions(const timelineui::ItemOptions& value)
    {
        _p->timelineWidget->setItemOptions(value);
    }

    void TimelineWidget::_initializeGL()
    {
        TLRENDER_P();

        gl::initGLAD();

        if (auto context = p.context.lock())
        {
            try
            {
                p.render = timeline_gl::Render::create(context);
                CHECK_GL;
                const std::string vertexSource =
                    "#version 410\n"
                    "\n"
                    "in vec3 vPos;\n"
                    "in vec2 vTexture;\n"
                    "out vec2 fTexture;\n"
                    "\n"
                    "uniform struct Transform\n"
                    "{\n"
                    "    mat4 mvp;\n"
                    "} transform;\n"
                    "\n"
                    "void main()\n"
                    "{\n"
                    "    gl_Position = transform.mvp * vec4(vPos, 1.0);\n"
                    "    fTexture = vTexture;\n"
                    "}\n";
                const std::string fragmentSource =
                    "#version 410\n"
                    "\n"
                    "in vec2 fTexture;\n"
                    "out vec4 fColor;\n"
                    "\n"
                    "uniform sampler2D textureSampler;\n"
                    "\n"
                    "void main()\n"
                    "{\n"
                    "    fColor = texture(textureSampler, fTexture);\n"
                    "}\n";
                p.shader = gl::Shader::create(vertexSource, fragmentSource);
                CHECK_GL;
            }
            catch (const std::exception& e)
            {
                context->log(
                    "mrv::mrvTimelineWidget", e.what(), log::Type::Error);
            }

            _sizeHintEvent();
        }
    }

    void TimelineWidget::resize(int X, int Y, int W, int H)
    {
        TLRENDER_P();

        Fl_Gl_Window::resize(X, Y, W, H);

        _setGeometry();
        _clipEvent();

        p.buffer.reset(); // needed

        if (p.thumbnailWindow)
        {
            repositionThumbnail();
        }
    }

    void TimelineWidget::draw()
    {
        TLRENDER_P();
        const math::Size2i renderSize(pixel_w(), pixel_h());

        make_current();

        if (!valid())
        {
            _initializeGL();
            valid(1);
        }

        if (p.player && p.player->hasAnnotations())
        {
            const auto& times = p.player->getAnnotationTimes();
            if (p.annotationTimes != times)
            {
                p.annotationTimes = times;
                std::vector<int> markers;
                markers.reserve(times.size());
                for (const auto& time : times)
                {
                    markers.push_back(std::round(time.value()));
                }
                p.timelineWidget->setFrameMarkers(markers);
            }
        }

        if (_getDrawUpdate(p.timelineWindow) || !p.buffer)
        {
            try
            {
                if (renderSize.isValid())
                {
                    gl::OffscreenBufferOptions offscreenBufferOptions;
                    offscreenBufferOptions.colorType =
                        image::PixelType::RGBA_U8;
                    if (gl::doCreate(
                            p.buffer, renderSize, offscreenBufferOptions))
                    {
                        p.buffer = gl::OffscreenBuffer::create(
                            renderSize, offscreenBufferOptions);
                    }
                }
                else
                {
                    p.buffer.reset();
                }

                if (p.render && p.buffer)
                {
                    gl::OffscreenBufferBinding binding(p.buffer);
                    timeline::RenderOptions renderOptions;
                    renderOptions.clearColor =
                        p.style->getColorRole(ui::ColorRole::Window);
                    p.render->begin(renderSize, renderOptions);
                    ui::DrawEvent drawEvent(
                        p.style, p.iconLibrary, p.render, p.fontSystem);
                    p.render->setClipRectEnabled(true);
                    _drawEvent(
                        p.timelineWindow, math::Box2i(renderSize), drawEvent);
                    p.render->setClipRectEnabled(false);
                    p.render->end();
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(e.what());
            }
        }

        if (p.ui->uiPrefs->uiPrefsBlitTimeline->value() == kNoBlit)
        {
            glViewport(0, 0, renderSize.w, renderSize.h);
            glClearColor(0.F, 0.F, 0.F, 0.F);
            glClear(GL_COLOR_BUFFER_BIT);

            if (p.buffer)
            {
                p.shader->bind();
                const auto pm = math::ortho(
                    0.F, static_cast<float>(renderSize.w), 0.F,
                    static_cast<float>(renderSize.h), -1.F, 1.F);
                p.shader->setUniform("transform.mvp", pm);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, p.buffer->getColorID());

                const auto mesh =
                    geom::box(math::Box2i(0, 0, renderSize.w, renderSize.h));
                if (!p.vbo)
                {
                    p.vbo = gl::VBO::create(
                        mesh.triangles.size() * 3,
                        gl::VBOType::Pos2_F32_UV_U16);
                }
                if (p.vbo)
                {
                    p.vbo->copy(convert(mesh, gl::VBOType::Pos2_F32_UV_U16));
                }

                if (!p.vao && p.vbo)
                {
                    p.vao = gl::VAO::create(
                        gl::VBOType::Pos2_F32_UV_U16, p.vbo->getID());
                }
                if (p.vao && p.vbo)
                {
                    p.vao->bind();
                    p.vao->draw(GL_TRIANGLES, 0, p.vbo->getSize());
                }
            }
        }
        else
        {
            if (p.buffer)
            {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, p.buffer->getID());
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // 0 is screen

                glBlitFramebuffer(
                    0, 0, renderSize.w, renderSize.h, 0, 0, renderSize.w,
                    renderSize.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
        }
    }

    int TimelineWidget::enterEvent()
    {
        TLRENDER_P();

        bool takeFocus = true;
        Fl_Widget* focusWidget = Fl::focus();
        TimelineClass* c = p.ui->uiTimeWindow;
        if (focusWidget == c->uiFrame || focusWidget == c->uiStartFrame ||
            focusWidget == c->uiEndFrame)
            takeFocus = false;
        // if (Fl::focus() == nullptr)
        if (takeFocus)
            take_focus();
        p.timelineWindow->cursorEnter(true);
        return 1;
    }

    int TimelineWidget::leaveEvent()
    {
        TLRENDER_P();
        p.timelineWindow->cursorEnter(false);
        focus(p.ui->uiView);
        return 1;
    }

    namespace
    {
        int fromFLTKModifiers()
        {
            int out = 0;
            if (Fl::event_key(FL_Shift_L) || Fl::event_key(FL_Shift_R))
            {
                out |= static_cast<int>(ui::KeyModifier::Shift);
            }
            if (Fl::event_key(FL_Control_L) || Fl::event_key(FL_Control_R))
            {
                out |= static_cast<int>(ui::KeyModifier::Control);
            }
            if (Fl::event_key(FL_Alt_L) || Fl::event_key(FL_Alt_R))
            {
                out |= static_cast<int>(ui::KeyModifier::Alt);
            }
            return out;
        }
    } // namespace

    int TimelineWidget::mousePressEvent(int button, bool on, int modifiers)
    {
        TLRENDER_P();
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
        {
            Message message;
            message["command"] = "Timeline Mouse Press";
            message["button"] = button;
            message["on"] = on;
            message["modifiers"] = modifiers;
            tcp->pushMessage(message);
        }
        if (p.draggingClip)
        {
            makePathsAbsolute(p.player, p.ui);
        }
        p.timelineWindow->mouseButton(button, on, modifiers);
        return 1;
    }

    int TimelineWidget::mousePressEvent()
    {
        TLRENDER_P();
        take_focus();

        bool isNDI = file::isTemporaryNDI(p.player->path());
        if (isNDI)
            return 0;

        int button = -1;
        int modifiers = fromFLTKModifiers();
        if (Fl::event_button1())
        {
            button = 0;
            if (modifiers == 0)
            {
                int ok = _seek();
                if (!p.draggingClip && ok)
                    return 1;
            }
        }
        else if (Fl::event_button2())
        {
            button = 0;
            modifiers = static_cast<int>(ui::KeyModifier::Control);
        }
        else
        {
            return 0;
        }

        mousePressEvent(button, true, modifiers);
        return 1;
    }

    int TimelineWidget::mouseDragEvent(const int X, const int Y)
    {
        TLRENDER_P();
        bool isNDI = file::isTemporaryNDI(p.player->path());
        if (isNDI)
            return 0;

        int modifiers = fromFLTKModifiers();
        if (Fl::event_button1())
        {
            if (modifiers == 0)
            {
                int ok = _seek();
                if (!p.draggingClip && ok)
                    return 1;
            }
        }
        else if (Fl::event_button2())
        {
            // left empty on purpose
        }
        else
        {
            return 0;
        }
        mouseMoveEvent(X, Y);
        return 1;
    }

    int TimelineWidget::mouseReleaseEvent(
        const int X, const int Y, int button, bool on, int modifiers)
    {
        TLRENDER_P();

        bool isNDI = file::isTemporaryNDI(p.player->path());
        if (isNDI)
            return 0;

        if (button == 1)
        {
            int ok = _seek();
            if (!p.draggingClip && ok)
                return 1;
        }
        mouseMoveEvent(X, Y);
        mousePressEvent(button, on, modifiers);
        if (p.draggingClip)
        {
            toOtioFile(p.player, p.ui);
            p.ui->uiView->redrawWindows();
            panel::redrawThumbnails();
            p.draggingClip = false;
        }
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
        {
            Message message;
            message["command"] = "Timeline Mouse Release";
            message["X"] = static_cast<float>(X) / pixel_w();
            message["Y"] = static_cast<float>(Y) / pixel_h();
            message["button"] = button;
            message["on"] = on;
            message["modifiers"] = modifiers;
            tcp->pushMessage(message);
        }
        return 1;
    }

    int TimelineWidget::mouseReleaseEvent()
    {
        int button = 0;
        if (Fl::event_button1())
            button = 1;
        mouseReleaseEvent(
            Fl::event_x(), Fl::event_y(), button, false, fromFLTKModifiers());
        return 1;
    }

    int TimelineWidget::mouseMoveEvent()
    {
        TLRENDER_P();
        mouseMoveEvent(Fl::event_x(), Fl::event_y());
        return 1;
    }

    void TimelineWidget::mouseMoveEvent(const int X, const int Y)
    {
        TLRENDER_P();
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
        {
            Message message;
            message["command"] = "Timeline Mouse Move";
            message["X"] = static_cast<float>(X) / pixel_w();
            message["Y"] = static_cast<float>(Y) / pixel_h();
            tcp->pushMessage(message);
        }
        const auto now = std::chrono::steady_clock::now();
        const auto diff = std::chrono::duration<float>(now - p.mouseWheelTimer);
        const float delta = Fl::event_dy() / 8.F / 15.F;
        p.mouseWheelTimer = now;
        p.timelineWindow->cursorPos(math::Vector2i(_toUI(X), _toUI(Y)));
    }

    void
    TimelineWidget::scrollEvent(const float X, const float Y, int modifiers)
    {
        TLRENDER_P();
        math::Vector2f pos(X, Y);
        p.timelineWindow->scroll(pos, modifiers);

        Message message;
        message["command"] = "Timeline Widget Scroll";
        message["X"] = X;
        message["Y"] = Y;
        message["modifiers"] = modifiers;
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
            tcp->pushMessage(message);
    }

    int TimelineWidget::wheelEvent()
    {
        TLRENDER_P();
        const auto now = std::chrono::steady_clock::now();
        const auto diff = std::chrono::duration<float>(now - p.mouseWheelTimer);
        const float delta = Fl::event_dy() / 8.F / 15.F;
        p.mouseWheelTimer = now;
        scrollEvent(Fl::event_dx() / 8.F / 15.F, -delta, fromFLTKModifiers());
        return 1;
    }

    namespace
    {
        ui::Key fromFLTKKey(unsigned key)
        {
            ui::Key out = ui::Key::Unknown;

#if defined(FLTK_USE_WAYLAND)
            if (key >= 'A' && key <= 'Z')
            {
                key = tolower(key);
            }
#endif
            switch (key)
            {
            case ' ':
                out = ui::Key::Space;
                break;
            case '\'':
                out = ui::Key::Apostrophe;
                break;
            case ',':
                out = ui::Key::Comma;
                break;
            case '-':
                out = ui::Key::Minus;
                break;
            case '.':
                out = ui::Key::Period;
                break;
            case '/':
                out = ui::Key::Slash;
                break;
            case '0':
                out = ui::Key::_0;
                break;
            case '1':
                out = ui::Key::_1;
                break;
            case '2':
                out = ui::Key::_2;
                break;
            case '3':
                out = ui::Key::_3;
                break;
            case '4':
                out = ui::Key::_4;
                break;
            case '5':
                out = ui::Key::_5;
                break;
            case '6':
                out = ui::Key::_6;
                break;
            case '7':
                out = ui::Key::_7;
                break;
            case '8':
                out = ui::Key::_8;
                break;
            case '9':
                out = ui::Key::_9;
                break;
            case ';':
                out = ui::Key::Semicolon;
                break;
            case '=':
                out = ui::Key::Equal;
                break;
            case 'a':
                out = ui::Key::A;
                break;
            case 'b':
                out = ui::Key::B;
                break;
            case 'c':
                out = ui::Key::C;
                break;
            case 'd':
                out = ui::Key::D;
                break;
            case 'e':
                out = ui::Key::E;
                break;
            case 'f':
                out = ui::Key::F;
                break;
            case 'g':
                out = ui::Key::G;
                break;
            case 'h':
                out = ui::Key::H;
                break;
            case 'i':
                out = ui::Key::I;
                break;
            case 'j':
                out = ui::Key::J;
                break;
            case 'k':
                out = ui::Key::K;
                break;
            case 'l':
                out = ui::Key::L;
                break;
            case 'm':
                out = ui::Key::M;
                break;
            case 'n':
                out = ui::Key::N;
                break;
            case 'o':
                out = ui::Key::O;
                break;
            case 'p':
                out = ui::Key::P;
                break;
            case 'q':
                out = ui::Key::Q;
                break;
            case 'r':
                out = ui::Key::R;
                break;
            case 's':
                out = ui::Key::S;
                break;
            case 't':
                out = ui::Key::T;
                break;
            case 'u':
                out = ui::Key::U;
                break;
            case 'v':
                out = ui::Key::V;
                break;
            case 'w':
                out = ui::Key::W;
                break;
            case 'x':
                out = ui::Key::X;
                break;
            case 'y':
                out = ui::Key::Y;
                break;
            case 'z':
                out = ui::Key::Z;
                break;
            case '[':
                out = ui::Key::LeftBracket;
                break;
            case '\\':
                out = ui::Key::Backslash;
                break;
            case ']':
                out = ui::Key::RightBracket;
                break;
            case 0xfe51: // @todo: is there a Fl_ shortcut for this?
                out = ui::Key::GraveAccent;
                break;
            case FL_Escape:
                out = ui::Key::Escape;
                break;
            case FL_Enter:
                out = ui::Key::Enter;
                break;
            case FL_Tab:
                out = ui::Key::Tab;
                break;
            case FL_BackSpace:
                out = ui::Key::Backspace;
                break;
            case FL_Insert:
                out = ui::Key::Insert;
                break;
            case FL_Delete:
                out = ui::Key::Delete;
                break;
            case FL_Right:
                out = ui::Key::Right;
                break;
            case FL_Left:
                out = ui::Key::Left;
                break;
            case FL_Down:
                out = ui::Key::Down;
                break;
            case FL_Up:
                out = ui::Key::Up;
                break;
            case FL_Page_Up:
                out = ui::Key::PageUp;
                break;
            case FL_Page_Down:
                out = ui::Key::PageDown;
                break;
            case FL_Home:
                out = ui::Key::Home;
                break;
            case FL_End:
                out = ui::Key::End;
                break;
            case FL_Caps_Lock:
                out = ui::Key::CapsLock;
                break;
            case FL_Scroll_Lock:
                out = ui::Key::ScrollLock;
                break;
            case FL_Num_Lock:
                out = ui::Key::NumLock;
                break;
            case FL_Print:
                out = ui::Key::PrintScreen;
                break;
            case FL_Pause:
                out = ui::Key::Pause;
                break;
            case FL_F + 1:
                out = ui::Key::F1;
                break;
            case FL_F + 2:
                out = ui::Key::F2;
                break;
            case FL_F + 3:
                out = ui::Key::F3;
                break;
            case FL_F + 4:
                out = ui::Key::F4;
                break;
            case FL_F + 5:
                out = ui::Key::F5;
                break;
            case FL_F + 6:
                out = ui::Key::F6;
                break;
            case FL_F + 7:
                out = ui::Key::F7;
                break;
            case FL_F + 8:
                out = ui::Key::F8;
                break;
            case FL_F + 9:
                out = ui::Key::F9;
                break;
            case FL_F + 10:
                out = ui::Key::F10;
                break;
            case FL_F + 11:
                out = ui::Key::F11;
                break;
            case FL_F + 12:
                out = ui::Key::F12;
                break;
            case FL_Shift_L:
                out = ui::Key::LeftShift;
                break;
            case FL_Control_L:
                out = ui::Key::LeftControl;
                break;
            case FL_Alt_L:
                out = ui::Key::LeftAlt;
                break;
            case FL_Meta_L:
                out = ui::Key::LeftSuper;
                break;
            case FL_Meta_R:
                out = ui::Key::RightSuper;
                break;
            }
            return out;
        }
    } // namespace

    void TimelineWidget::frameView()
    {
        TLRENDER_P();
        unsigned key = _changeKey(kFitScreen.hotkey());
        if (p.player)
        {
            auto innerPlayer = p.player->player();
            p.timeRange = innerPlayer->getTimeRange();
        }
        p.timelineWindow->key(fromFLTKKey(key), true, 0);
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
        {
            Message message;
            message["command"] = "Timeline Fit";
            tcp->pushMessage(message);
        }
    }

    int TimelineWidget::keyPressEvent(unsigned key, const int modifiers)
    {
        TLRENDER_P();
        // First, check if it is one of the menu shortcuts
        int ret = p.ui->uiMenuBar->handle(FL_SHORTCUT);
        if (ret)
            return ret;
        if (kToggleEditMode.match(key))
        {
            p.ui->uiEdit->do_callback();
            return 1;
        }
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
        {
            Message message;
            message["command"] = "Timeline Key Press";
            message["value"] = key;
            message["modifiers"] = modifiers;
            tcp->pushMessage(message);
        }

        key = _changeKey(key);
        p.timelineWindow->key(fromFLTKKey(key), true, modifiers);
        return 1;
    }

    int TimelineWidget::keyPressEvent()
    {
        TLRENDER_P();
        unsigned key = Fl::event_key();
        keyPressEvent(key, fromFLTKModifiers());
        return 1;
    }

    int TimelineWidget::keyReleaseEvent(unsigned key, const int modifiers)
    {
        TLRENDER_P();
        bool send = App::ui->uiPrefs->SendTimeline->value();
        if (send)
        {
            Message message;
            message["command"] = "Timeline Key Release";
            message["value"] = key;
            message["modifiers"] = modifiers;
            tcp->pushMessage(message);
        }
        key = _changeKey(key);
        p.timelineWindow->key(fromFLTKKey(key), false, modifiers);
        return 1;
    }

    int TimelineWidget::keyReleaseEvent()
    {
        TLRENDER_P();
        keyReleaseEvent(Fl::event_key(), fromFLTKModifiers());
        return 1;
    }

    void TimelineWidget::timerEvent_cb(void* d)
    {
        TimelineWidget* o = static_cast<TimelineWidget*>(d);
        o->timerEvent();
    }

    void TimelineWidget::timerEvent()
    {
        TLRENDER_P();

        //! \bug This guard is needed since the timer event can be called during
        //! destruction?
        if (_p)
        {
            _tickEvent();

            if (_getSizeUpdate(p.timelineWindow))
            {
                _sizeHintEvent();
                _setGeometry();
                _clipEvent();
                // updateGeometry();  // Qt function?
            }

            if (_getDrawUpdate(p.timelineWindow))
            {
                redraw();
            }
        }
        Fl::repeat_timeout(kTimeout, (Fl_Timeout_Handler)timerEvent_cb, this);
    }

    int TimelineWidget::handle(int event)
    {
        TLRENDER_P();
        if (!p.player)
            return 0;
        switch (event)
        {
        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;
        case FL_ENTER:
            // cursor(FL_CURSOR_DEFAULT);
            if (p.thumbnailWindow && p.player &&
                p.ui->uiPrefs->uiPrefsTimelineThumbnails->value())
            {
                requestThumbnail(true);
                p.thumbnailWindow->show();
            }
            return enterEvent();
        case FL_LEAVE:
            if (p.ui->uiPrefs->uiPrefsTimelineThumbnails->value())
            {
                _cancelRequests();
                hideThumbnail();
            }
            return leaveEvent();
        case FL_PUSH:
            if (p.ui->uiPrefs->uiPrefsTimelineThumbnails->value())
                requestThumbnail(true);
            return mousePressEvent();
        case FL_DRAG:
            return mouseDragEvent(Fl::event_x(), Fl::event_y());
        case FL_RELEASE:
            panel::redrawThumbnails();
            return mouseReleaseEvent();
        case FL_MOVE:
            requestThumbnail(true);
            return mouseMoveEvent();
        case FL_MOUSEWHEEL:
            return wheelEvent();
        case FL_KEYDOWN:
        {
            return keyPressEvent();
        }
        case FL_KEYUP:
            return keyReleaseEvent();
        case FL_HIDE:
        {
            if (p.ui->uiPrefs->uiPrefsTimelineThumbnails->value())
            {
                _cancelRequests();
                hideThumbnail();
            }
            refresh();
            valid(0);
            return Fl_Gl_Window::handle(event);
        }
        }
        int out = Fl_Gl_Window::handle(event);
        return out;
    }

    int TimelineWidget::_toUI(int value) const
    {
        TimelineWidget* self = const_cast<TimelineWidget*>(this);
        const float devicePixelRatio = self->pixels_per_unit();
        return value * devicePixelRatio;
    }

    math::Vector2i TimelineWidget::_toUI(const math::Vector2i& value) const
    {
        TimelineWidget* self = const_cast<TimelineWidget*>(this);
        const float devicePixelRatio = self->pixels_per_unit();
        return value * devicePixelRatio;
    }

    int TimelineWidget::_fromUI(int value) const
    {
        TimelineWidget* self = const_cast<TimelineWidget*>(this);
        const float devicePixelRatio = self->pixels_per_unit();
        return devicePixelRatio > 0.F ? (value / devicePixelRatio) : 0.F;
    }

    math::Vector2i TimelineWidget::_fromUI(const math::Vector2i& value) const
    {
        TimelineWidget* self = const_cast<TimelineWidget*>(this);
        const float devicePixelRatio = self->pixels_per_unit();
        return devicePixelRatio > 0.F ? (value / devicePixelRatio)
                                      : math::Vector2i();
    }

    //! Routine to turn mrv2's hotkeys into Darby's shortcuts
    unsigned TimelineWidget::_changeKey(unsigned key)
    {
        if (key == kFitScreen.hotkey())
            key = '0'; // Darby uses 0 to frame view
        else if (key == kFitAll.hotkey())
            key = '0'; // Darby uses 0 to frame view
        return key;
    }

    void TimelineWidget::_styleUpdate()
    {
        /*TLRENDER_P();
          const auto palette = this->palette();
          p.style->setColorRole(
          ui::ColorRole::Window,
          fromQt(palette.color(QPalette::ColorRole::Window)));
          p.style->setColorRole(
          ui::ColorRole::Base,
          fromQt(palette.color(QPalette::ColorRole::Base)));
          p.style->setColorRole(
          ui::ColorRole::Button,
          fromQt(palette.color(QPalette::ColorRole::Button)));
          p.style->setColorRole(
          ui::ColorRole::Text,
          fromQt(palette.color(QPalette::ColorRole::WindowText)));*/
    }

    otime::RationalTime TimelineWidget::_posToTime(int value) noexcept
    {
        TLRENDER_P();

        otime::RationalTime out = time::invalidTime;
        if (p.player && p.timelineWidget)
        {
            _setGeometry(); // needed, as Linux could have issues when
                            // dragging the window to the borders.
            const math::Box2i& geometry =
                p.timelineWidget->getTimelineItemGeometry();
            const double normalized =
                (value - geometry.min.x) / static_cast<double>(geometry.w());
            out = time::round(
                p.timeRange.start_time() +
                otime::RationalTime(
                    p.timeRange.duration().value() * normalized,
                    p.timeRange.duration().rate()));
            out = math::clamp(
                out, p.timeRange.start_time(),
                p.timeRange.end_time_inclusive());
        }
        return out;
    }

    void TimelineWidget::refresh()
    {
        TLRENDER_P();
        p.render.reset();
        p.buffer.reset();
        p.shader.reset();
        p.vbo.reset();
        p.vao.reset();
    }

    void TimelineWidget::setUnits(TimeUnits value)
    {
        TLRENDER_P();
        p.units = value;
        auto timeUnitsModel = _p->ui->app->timeUnitsModel();
        timeUnitsModel->setTimeUnits(
            static_cast<tl::timeline::TimeUnits>(value));
        TimelineClass* c = _p->ui->uiTimeWindow;
        c->uiStartFrame->setUnits(value);
        c->uiEndFrame->setUnits(value);
        c->uiFrame->setUnits(value);
        redraw();
    }

    void TimelineWidget::moveCallback(
        const std::vector<tl::timeline::MoveData>& moves)
    {
        TLRENDER_P();
        edit_store_undo(p.player, p.ui);
        edit_clear_redo(p.ui);
        edit_move_clip_annotations(moves, p.ui);
    }

    void TimelineWidget::_thumbnailEvent()
    {
        TLRENDER_P();
        
        // Check if the I/O information is finished.
        if (p.infoRequest.future.valid() &&
            p.infoRequest.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            p.ioInfo = std::make_shared<io::Info>(p.infoRequest.future.get());
        }

        // Check if any thumbnails are finished.
        auto i = p.thumbnailRequests.begin();
        while (i != p.thumbnailRequests.end())
        {
            if (i->second.future.valid() &&
                i->second.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const auto image = i->second.future.get();
                p.thumbnails[io::getCacheKey(p.path, i->first, p.ioOptions)] = image;
                i = p.thumbnailRequests.erase(i);
                
                const auto W = image->getWidth();
                const auto H = image->getHeight();
                const int bytes = image->getDataByteCount();
                const int depth = bytes / W / H;
                uint8_t* data = image->getData();
                flipImageInY(data, W, H, depth);
                
                _updateThumbnail(image);
            }
            else
            {
                ++i;
            }
        }
    }

    void TimelineWidget::_tickEvent()
    {
        TLRENDER_P();
        ui::TickEvent tickEvent(p.style, p.iconLibrary, p.fontSystem);
        _tickEvent(p.timelineWindow, true, true, tickEvent);

        _thumbnailEvent();
    }

    void TimelineWidget::_tickEvent(
        const std::shared_ptr<ui::IWidget>& widget, bool visible, bool enabled,
        const ui::TickEvent& event)
    {
        TLRENDER_P();
        const bool parentsVisible = visible && widget->isVisible(false);
        const bool parentsEnabled = enabled && widget->isEnabled(false);
        for (const auto& child : widget->getChildren())
        {
            _tickEvent(child, parentsVisible, parentsEnabled, event);
        }
        widget->tickEvent(visible, enabled, event);
    }

    bool TimelineWidget::_getSizeUpdate(
        const std::shared_ptr<ui::IWidget>& widget) const
    {
        bool out = widget->getUpdates() & ui::Update::Size;
        if (out)
        {
            // std::cout << "Size update: " << widget->getObjectName() <<
            // std::endl;
        }
        else
        {
            for (const auto& child : widget->getChildren())
            {
                out |= _getSizeUpdate(child);
            }
        }
        return out;
    }

    void TimelineWidget::_sizeHintEvent()
    {
        TLRENDER_P();
        const float devicePixelRatio = this->pixels_per_unit();
        ui::SizeHintEvent sizeHintEvent(
            p.style, p.iconLibrary, p.fontSystem, devicePixelRatio);
        _sizeHintEvent(p.timelineWindow, sizeHintEvent);
    }

    void TimelineWidget::_sizeHintEvent(
        const std::shared_ptr<ui::IWidget>& widget,
        const ui::SizeHintEvent& event)
    {
        for (const auto& child : widget->getChildren())
        {
            _sizeHintEvent(child, event);
        }
        widget->sizeHintEvent(event);
    }

    void TimelineWidget::_setGeometry()
    {
        TLRENDER_P();
        const math::Box2i geometry(0, 0, _toUI(w()), _toUI(h()));
        p.timelineWindow->setGeometry(geometry);
    }

    void TimelineWidget::_clipEvent()
    {
        TLRENDER_P();
        const math::Box2i geometry(0, 0, _toUI(w()), _toUI(h()));
        _clipEvent(p.timelineWindow, geometry, false);
    }

    void TimelineWidget::_clipEvent(
        const std::shared_ptr<ui::IWidget>& widget, const math::Box2i& clipRect,
        bool clipped)
    {
        const math::Box2i& g = widget->getGeometry();
        clipped |= !g.intersects(clipRect);
        clipped |= !widget->isVisible(false);
        const math::Box2i clipRect2 = g.intersect(clipRect);
        widget->clipEvent(clipRect2, clipped);
        const math::Box2i childrenClipRect =
            widget->getChildrenClipRect().intersect(clipRect2);
        for (const auto& child : widget->getChildren())
        {
            const math::Box2i& childGeometry = child->getGeometry();
            _clipEvent(
                child, childGeometry.intersect(childrenClipRect), clipped);
        }
    }

    bool TimelineWidget::_getDrawUpdate(
        const std::shared_ptr<ui::IWidget>& widget) const
    {
        bool out = false;
        if (!widget->isClipped())
        {
            out = widget->getUpdates() & ui::Update::Draw;
            if (out)
            {
                // std::cout << "Draw update: " << widget->getObjectName() <<
                // std::endl;
            }
            else
            {
                for (const auto& child : widget->getChildren())
                {
                    out |= _getDrawUpdate(child);
                }
            }
        }
        return out;
    }

    void TimelineWidget::_drawEvent(
        const std::shared_ptr<ui::IWidget>& widget, const math::Box2i& drawRect,
        const ui::DrawEvent& event)
    {
        const math::Box2i& g = widget->getGeometry();
        if (!widget->isClipped() && g.w() > 0 && g.h() > 0)
        {
            event.render->setClipRect(drawRect);
            widget->drawEvent(drawRect, event);
            const math::Box2i childrenClipRect =
                widget->getChildrenClipRect().intersect(drawRect);
            event.render->setClipRect(childrenClipRect);
            for (const auto& child : widget->getChildren())
            {
                const math::Box2i& childGeometry = child->getGeometry();
                if (childGeometry.intersects(childrenClipRect))
                {
                    _drawEvent(
                        child, childGeometry.intersect(childrenClipRect),
                        event);
                }
            }
            event.render->setClipRect(drawRect);
            widget->drawOverlayEvent(drawRect, event);
        }
    }
    
    void TimelineWidget::_cancelRequests()
    {
        TLRENDER_P();
        
        std::vector<uint64_t> ids;
        if (p.infoRequest.future.valid())
        {
            ids.push_back(p.infoRequest.id);
            p.infoRequest = ui::InfoRequest();
        }
        for (const auto& i : p.thumbnailRequests)
        {
            ids.push_back(i.second.id);
        }
        p.thumbnailRequests.clear();
        p.thumbnailGenerator->cancelRequests(ids);
    }

} // namespace mrv
