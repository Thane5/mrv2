// SPDX-License-Identifier: BSD-3-Clause
// mrv2 (mrViewer2)
// Copyright Contributors to the mrv2 Project. All rights reserved.


#include <cinttypes>


#include <tlCore/FontSystem.h>
#include <tlCore/Mesh.h>

#include <tlGL/Mesh.h>
#include <tlGL/OffscreenBuffer.h>
#include <tlGL/Render.h>
#include <tlGL/Shader.h>
#include <tlGL/Util.h>

#include <Imath/ImathMatrix.h>

#include <tlGlad/gl.h>

// mrViewer includes
#include <mrvCore/mrvUtil.h>
#include <mrvCore/mrvSequence.h>
#include <mrvCore/mrvColorSpaces.h>

#include <mrvWidgets/mrvMultilineInput.h>

#include <mrvFl/mrvIO.h>
#include "mrvFl/mrvToolsCallbacks.h"
#include <mrvFl/mrvTimelinePlayer.h>
#include <mrViewer.h>

#include <mrvGL/mrvGLErrors.h>
#include <mrvGL/mrvGLUtil.h>
#include <mrvGL/mrvGLShape.h>
#include <mrvGL/mrvTimelineViewport.h>
#include <mrvGL/mrvTimelineViewportPrivate.h>
#include <mrvGL/mrvGLViewport.h>
#ifdef USE_ONE_PIXEL_LINES
#   include <mrvGL/mrvGLOutline.h>
#endif

#include <mrvApp/mrvSettingsObject.h>

#include <glm/gtc/matrix_transform.hpp>



// For main fltk event loop
#include <FL/Fl.H>

//! Define a variable, "gl", that references the private implementation.
#define TLRENDER_GL()                           \
    auto& gl = *_gl


namespace {
    const char* kModule = "view";
}

namespace mrv
{
    using namespace tl;


    struct Viewport::GLPrivate
    {
        std::weak_ptr<system::Context> context;

        // GL variables
        //! OpenGL Offscreen buffer
        std::shared_ptr<tl::gl::OffscreenBuffer> buffer = nullptr;
        std::shared_ptr<tl::gl::Render> render = nullptr;
        std::shared_ptr<tl::gl::Shader> shader    = nullptr;
        int index = 0;
        int nextIndex = 1;
        GLuint pboIds[2];
        std::shared_ptr<gl::VBO> vbo;
        std::shared_ptr<gl::VAO> vao;

#ifdef USE_ONE_PIXEL_LINES
        tl::gl::Outline              outline;
#endif

        //! We store really imaging::Color4f but since we need to reverse
        //! the R and B channels (as they are read in BGR order), we process
        //! floats.
        GLfloat*                 image = nullptr;
    };


    Viewport::Viewport( int X, int Y, int W, int H, const char* L ) :
        TimelineViewport( X, Y, W, H, L ),
        _gl( new GLPrivate )
    {
        mode( FL_RGB | FL_DOUBLE | FL_ALPHA | FL_STENCIL | FL_OPENGL3 );
    }


    Viewport::Viewport( int W, int H, const char* L ) :
        TimelineViewport( W, H, L ),
        _gl( new GLPrivate )
    {
        mode( FL_RGB | FL_DOUBLE | FL_ALPHA | FL_STENCIL | FL_OPENGL3 );
    }


    Viewport::~Viewport()
    {
        TLRENDER_GL();

        glDeleteBuffers(2, gl.pboIds);
    }

    void Viewport::setContext(
        const std::weak_ptr<system::Context>& context )
    {
        _gl->context = context;
    }

    void Viewport::_initializeGL()
    {
        TLRENDER_P();
        TLRENDER_GL();
        try
        {
            tl::gl::initGLAD();

            gl.index = 0;
            gl.nextIndex = 1;

            glGenBuffers( 2, gl.pboIds );

            if ( !gl.render )
            {
                if (auto context = gl.context.lock())
                {
                    gl.render = gl::Render::create(context);
                }
            }

            if ( !p.fontSystem )
            {
                if (auto context = gl.context.lock())
                {
                    p.fontSystem = imaging::FontSystem::create(context);
                }
            }

            if ( !gl.shader )
            {

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
                gl.shader = gl::Shader::create(vertexSource, fragmentSource);
            }
        }
        catch (const std::exception& e)
        {
            if (auto context = gl.context.lock())
            {
                context->log(
                    "mrv::Viewport",
                    e.what(),
                    log::Type::Error);
            }
        }
    }


    void Viewport::_drawCursor(const math::Matrix4x4f& mvp) const noexcept
    {
        TLRENDER_GL();
        TLRENDER_P();
        if ( p.actionMode != ActionMode::kScrub &&
             p.actionMode != ActionMode::kText &&
             p.actionMode != ActionMode::kSelection &&
             Fl::belowmouse() == this )
        {
            const imaging::Color4f color( 1.F, 1.F, 1.F, 1.F );
            std_any value;
            value = p.ui->app->settingsObject()->value( kPenSize );
            const float pen_size = std_any_cast<int>(value);
            drawCursor( gl.render, _getRaster(), pen_size, 2.0F,
                        color, mvp );
        }
    }


    void
    Viewport::_drawRectangleOutline(
        const math::BBox2i& box,
        const imaging::Color4f& color,
        const math::Matrix4x4f& mvp ) const noexcept
    {
        TLRENDER_GL();
#if USE_ONE_PIXEL_LINES
        gl.outline.drawRect( box, color, mvp );
#else
        drawRectOutline( gl.render, box, color, 2.F, mvp );
#endif

    }

    const imaging::Color4f* Viewport::image() const
    {
        return ( imaging::Color4f* ) ( _gl->image );
    }


    void Viewport::draw()
    {
        TLRENDER_P();
        TLRENDER_GL();

        if ( !valid() )
        {
            _initializeGL();
            valid(1);
        }

        const auto& renderSize = getRenderSize();
        try
        {
            if (renderSize.isValid())
            {
                gl::OffscreenBufferOptions offscreenBufferOptions;
                offscreenBufferOptions.colorType = imaging::PixelType::RGBA_F32;
                if (!p.displayOptions.empty())
                {
                    offscreenBufferOptions.colorFilters = p.displayOptions[0].imageFilters;
                }
                offscreenBufferOptions.depth = gl::OffscreenDepth::_24;
                offscreenBufferOptions.stencil = gl::OffscreenStencil::_8;
                if (gl::doCreate(gl.buffer, renderSize, offscreenBufferOptions))
                {
                    gl.buffer = gl::OffscreenBuffer::create(renderSize, offscreenBufferOptions);
                    unsigned dataSize = renderSize.w * renderSize.h * 4
                                        * sizeof(GLfloat);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, gl.pboIds[0]);
                    glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, 0,
                                 GL_STREAM_READ);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, gl.pboIds[1]);
                    glBufferData(GL_PIXEL_PACK_BUFFER, dataSize, 0,
                                 GL_STREAM_READ);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    CHECK_GL;
                }
            }
            else
            {
                gl.buffer.reset();
            }

            if (gl.buffer)
            {
                gl::OffscreenBufferBinding binding(gl.buffer);
                gl.render->setColorConfig(p.colorConfigOptions);
                gl.render->setLUT(p.lutOptions);
                gl.render->begin(renderSize);
                gl.render->drawVideo(
                    p.videoData,
                    timeline::tiles(p.compareOptions.mode,
                                    _getTimelineSizes()),
                    p.imageOptions,
                    p.displayOptions,
                    p.compareOptions);
                if (p.masking > 0.0001F ) _drawCropMask( renderSize );
                gl.render->end();
            }
        }
        catch (const std::exception& e)
        {
            if (auto context = gl.context.lock())
            {
                context->log(
                    "mrv::Viewport",
                    e.what(),
                    log::Type::Error);
            }
        }

        const auto viewportSize = getViewportSize();
        glViewport(
            0,
            0,
            GLsizei(viewportSize.w),
            GLsizei(viewportSize.h));

        float r, g, b, a = 0.0f;
        if ( !p.presentation )
        {
            uint8_t ur, ug, ub;
            Fl::get_color( p.ui->uiPrefs->uiPrefsViewBG->color(), ur, ug, ub );
            r = ur / 255.0f;
            g = ur / 255.0f;
            b = ur / 255.0f;
        }
        else
        {
            r = g = b = a = 0.0f;
        }

        glClearColor( r, g, b, a );
        glClear(GL_COLOR_BUFFER_BIT);

        if (gl.buffer)
        {
            gl.shader->bind();
            glm::mat4x4 vm(1.F);
            vm = glm::translate(vm, glm::vec3(p.viewPos.x, p.viewPos.y, 0.F));
            vm = glm::scale(vm, glm::vec3(p.viewZoom, p.viewZoom, 1.F));
            const glm::mat4x4 pm = glm::ortho(
                0.F,
                static_cast<float>(viewportSize.w),
                0.F,
                static_cast<float>(viewportSize.h),
                -1.F,
                1.F);
            glm::mat4x4 vpm = pm * vm;
            auto mvp = math::Matrix4x4f(
                vpm[0][0], vpm[0][1], vpm[0][2], vpm[0][3],
                vpm[1][0], vpm[1][1], vpm[1][2], vpm[1][3],
                vpm[2][0], vpm[2][1], vpm[2][2], vpm[2][3],
                vpm[3][0], vpm[3][1], vpm[3][2], vpm[3][3] );

            gl.shader->setUniform("transform.mvp", mvp);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gl.buffer->getColorID());

            geom::TriangleMesh3 mesh;
            mesh.v.push_back(math::Vector3f(0.F, 0.F, 0.F));
            mesh.t.push_back(math::Vector2f(0.F, 0.F));
            mesh.v.push_back(math::Vector3f(renderSize.w, 0.F, 0.F));
            mesh.t.push_back(math::Vector2f(1.F, 0.F));
            mesh.v.push_back(math::Vector3f(renderSize.w, renderSize.h, 0.F));
            mesh.t.push_back(math::Vector2f(1.F, 1.F));
            mesh.v.push_back(math::Vector3f(0.F, renderSize.h, 0.F));
            mesh.t.push_back(math::Vector2f(0.F, 1.F));
            mesh.triangles.push_back(geom::Triangle3({
                        geom::Vertex3({ 1, 1, 0 }),
                        geom::Vertex3({ 2, 2, 0 }),
                        geom::Vertex3({ 3, 3, 0 })
                    }));
            mesh.triangles.push_back(geom::Triangle3({
                        geom::Vertex3({ 3, 3, 0 }),
                        geom::Vertex3({ 4, 4, 0 }),
                        geom::Vertex3({ 1, 1, 0 })
                    }));

            auto vboData = convert(
                mesh,
                gl::VBOType::Pos3_F32_UV_U16,
                math::SizeTRange(0, mesh.triangles.size() - 1));
            if (!gl.vbo)
            {
                gl.vbo = gl::VBO::create(mesh.triangles.size() * 3, gl::VBOType::Pos3_F32_UV_U16);
            }
            if (gl.vbo)
            {
                gl.vbo->copy(vboData);
            }

            if (!gl.vao && gl.vbo)
            {
                gl.vao = gl::VAO::create(gl::VBOType::Pos3_F32_UV_U16, gl.vbo->getID());
            }
            if (gl.vao && gl.vbo)
            {
                gl.vao->bind();
                gl.vao->draw(GL_TRIANGLES, 0, gl.vbo->getSize());

                math::BBox2i selection = p.colorAreaInfo.box = p.selection;
                if ( selection.min != selection.max )
                {
                    // Check min < max
                    if ( selection.min.x > selection.max.x )
                    {
                        float tmp = selection.max.x;
                        selection.max.x = selection.min.x;
                        selection.min.x = tmp;
                    }
                    if ( selection.min.y > selection.max.y )
                    {
                        float tmp = selection.max.y;
                        selection.max.y = selection.min.y;
                        selection.min.y = tmp;
                    }
                    // Copy it again in cae it changed
                    p.colorAreaInfo.box = selection;
                    _bindReadImage();
                }
                else
                {
                    gl.image = nullptr;
                }
                if ( colorAreaTool )
                {
                    _calculateColorArea( p.colorAreaInfo );
                    colorAreaTool->update( p.colorAreaInfo );
                }
                if ( histogramTool )
                {
                    histogramTool->update( p.colorAreaInfo );
                }
                if ( vectorscopeTool )
                {
                    vectorscopeTool->update( p.colorAreaInfo );
                }

                updatePixelBar();

                if ( gl.image )
                {
                    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                    gl.image = nullptr;
                }

                // back to conventional pixel operation
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);


                Fl_Color c = p.ui->uiPrefs->uiPrefsViewSelection->color();
                uint8_t r, g, b;
                Fl::get_color( c, r, g, b );

                const imaging::Color4f color(r / 255.F, g / 255.F,
                                             b / 255.F);

                if ( p.selection.min != p.selection.max )
                {
                    _drawRectangleOutline( p.selection, color, mvp );
                }

                if ( p.showAnnotations ) _drawAnnotations(mvp);
                if ( p.safeAreas ) _drawSafeAreas();

                _drawCursor(mvp);


            }

            if ( p.hudActive && p.hud != HudDisplay::kNone ) _drawHUD();
        }

        MultilineInput* w = getMultilineInput();
        if ( w )
        {
            std_any value;
            value = p.ui->app->settingsObject()->value( kFontSize );
            int font_size = std_any_cast<int>( value );
            double pixels_unit = pixels_per_unit();
            double pct = viewportSize.h / 1024.F;
            double fontSize = font_size * pct * p.viewZoom;
            w->textsize( fontSize );
            math::Vector2i pos( w->pos.x, w->pos.y );
            // This works to pan without a change in zoom!
            // pos.x = pos.x + p.viewPos.x / p.viewZoom
            //         - w->viewPos.x / w->viewZoom;
            // pos.y = pos.y - p.viewPos.y / p.viewZoom
            //         - w->viewPos.y / w->viewZoom;
            // cur.x = (cur.x - p.viewPos.x / pixels_unit) / p.viewZoom;
            // cur.y = (cur.y - p.viewPos.y / pixels_unit) / p.viewZoom;

            // pos.x = (pos.x - w->viewPos.x / pixels_unit) /
            //         w->viewZoom;
            // pos.y = (pos.y - w->viewPos.y / pixels_unit) /
            //         w->viewZoom;

            // pos.x += cur.x;
            // pos.y -= cur.y;

            // std::cerr << "pos=" << pos << std::endl;
            // std::cerr << "p.viewPos=" << p.viewPos << std::endl;
            // std::cerr << "END " << pos << std::endl;
            w->Fl_Widget::position( pos.x, pos.y );
        }


#ifdef USE_OPENGL2
        Fl_Gl_Window::draw_begin(); // Set up 1:1 projectionç
        Fl_Window::draw();          // Draw FLTK children
        glViewport(0, 0, viewportSize.w, viewportSize.h);
        if ( p.showAnnotations ) _drawAnnotationsGL2();
        Fl_Gl_Window::draw_end();   // Restore GL state
#else
        Fl_Gl_Window::draw();
#endif

    }

#ifdef USE_OPENGL2

    void Viewport::_drawAnnotationsGL2()
    {
        TLRENDER_P();
        TLRENDER_GL();

        const auto& player = getTimelinePlayer();
        if (!player) return;

        const auto& time = player->currentTime();
        int64_t frame = time.value();

        const auto& annotations = player->getAnnotations( p.ghostPrevious,
                                                          p.ghostNext );
        if ( !annotations.empty() )
        {
            glStencilMask(~0);
            glClear(GL_STENCIL_BUFFER_BIT);

            float pixel_unit = pixels_per_unit();
            const auto& viewportSize = getViewportSize();
            const auto& renderSize   = getRenderSize();

            for ( const auto& annotation : annotations )
            {
                int64_t annotationFrame = annotation->frame();
                float alphamult = 0.F;
                if ( frame == annotationFrame || annotation->allFrames() ) alphamult = 1.F;
                else
                {
                    if ( p.ghostPrevious )
                    {
                        for ( short i = p.ghostPrevious-1; i > 0; --i )
                        {
                            if ( frame - i == annotationFrame )
                            {
                                alphamult = 1.F - (float)i/p.ghostPrevious;
                                break;
                            }
                        }
                    }
                    if ( p.ghostNext )
                    {
                        for ( short i = 1; i < p.ghostNext; ++i )
                        {
                            if ( frame + i == annotationFrame )
                            {
                                alphamult = 1.F - (float)i/p.ghostNext;
                                break;
                            }
                        }
                    }
                }

                if ( alphamult == 0.F ) continue;

                const auto& shapes = annotation->shapes();
                math::Vector2i pos;

                pos.x = p.viewPos.x;
                pos.y = p.viewPos.y;
                pos.x /= pixel_unit;
                pos.y /= pixel_unit;
                glm::mat4x4 vm(1.F);
                vm = glm::translate(vm, glm::vec3(pos.x, pos.y, 0.F));
                vm = glm::scale(vm, glm::vec3(p.viewZoom, p.viewZoom, 1.F));

                // No projection matrix.  Thar's set by FLTK ( and we
                // reset it -- flip it in Y -- inside mrvGL2TextShape.cpp ).
                auto mvp = math::Matrix4x4f(
                    vm[0][0], vm[0][1], vm[0][2], vm[0][3],
                    vm[1][0], vm[1][1], vm[1][2], vm[1][3],
                    vm[2][0], vm[2][1], vm[2][2], vm[2][3],
                    vm[3][0], vm[3][1], vm[3][2], vm[3][3] );

                for ( auto& shape : shapes )
                {
                    auto textShape =
                        dynamic_cast< GL2TextShape* >( shape.get() );
                    if ( !textShape ) continue;

                    float a = shape->color.a;
                    shape->color.a *= alphamult;
                    textShape->pixels_per_unit = pixels_per_unit();
                    textShape->w    = w();
                    textShape->h    = h();
                    textShape->viewZoom = p.viewZoom;
                    shape->matrix = mvp;
                    shape->draw( gl.render );
                    shape->color.a = a;
                }
            }
        }
    }
#endif

    void Viewport::_drawAnnotations(math::Matrix4x4f& mvp)
    {
        TLRENDER_P();
        TLRENDER_GL();

        const auto& player = getTimelinePlayer();
        if (!player) return;

        const auto& time = player->currentTime();
        int64_t frame = time.value();

        const auto& annotations = player->getAnnotations( p.ghostPrevious,
                                                          p.ghostNext );
        if ( !annotations.empty() )
        {
            glStencilMask(~0);
            glClear(GL_STENCIL_BUFFER_BIT);
            glEnable( GL_STENCIL_TEST );

            const auto& viewportSize = getViewportSize();
            const auto& renderSize = getRenderSize();

            for ( const auto& annotation : annotations )
            {
                int64_t annotationFrame = annotation->frame();
                float alphamult = 0.F;
                if ( frame == annotationFrame || annotation->allFrames() ) alphamult = 1.F;
                else
                {
                    if ( p.ghostPrevious  )
                    {
                        for ( short i = p.ghostPrevious-1; i > 0; --i )
                        {
                            if ( frame - i == annotationFrame )
                            {
                                alphamult = 1.F - (float)i/p.ghostPrevious;
                                break;
                            }
                        }
                    }
                    if ( p.ghostNext )
                    {
                        for ( short i = 1; i < p.ghostNext; ++i )
                        {
                            if ( frame + i == annotationFrame )
                            {
                                alphamult = 1.F - (float)i/p.ghostNext;
                                break;
                            }
                        }
                    }
                }

                if ( alphamult == 0.F ) continue;

                // Shapes are drawn in reverse order, so the erase path works
                const auto& shapes = annotation->shapes();
                ShapeList::const_reverse_iterator i = shapes.rbegin();
                ShapeList::const_reverse_iterator e = shapes.rend();

                for ( ; i != e; ++i )
                {
                    const auto& shape = *i;
#ifdef USE_OPENGL2
                    auto gl2Shape = dynamic_cast< GL2TextShape* >( shape.get() );
                    if ( gl2Shape ) continue;
#else
                    auto textShape = dynamic_cast< GLTextShape* >( shape.get() );
                    if ( textShape && !textShape->text.empty() )
                    {
                        glm::mat4x4 vm(1.F);
                        vm = glm::translate(vm, glm::vec3(p.viewPos.x,
                                                          p.viewPos.y, 0.F));
                        vm = glm::scale(vm, glm::vec3(p.viewZoom, p.viewZoom, 1.F));
                        glm::mat4x4 pm = glm::ortho(
                            0.F,
                            static_cast<float>(viewportSize.w),
                            0.F,
                            static_cast<float>(viewportSize.h),
                            -1.F,
                            1.F);
                        glm::mat4x4 vpm = pm * vm;
                        vpm = glm::scale(vpm, glm::vec3(1.F, -1.F, 1.F));
                        mvp = math::Matrix4x4f(
                            vpm[0][0], vpm[0][1], vpm[0][2], vpm[0][3],
                            vpm[1][0], vpm[1][1], vpm[1][2], vpm[1][3],
                            vpm[2][0], vpm[2][1], vpm[2][2], vpm[2][3],
                            vpm[3][0], vpm[3][1], vpm[3][2], vpm[3][3] );
                    }
#endif
                    float a = shape->color.a;
                    shape->color.a *= alphamult;
                    shape->matrix = mvp;
                    shape->draw( gl.render );
                    shape->color.a = a;
                }
            }
            glDisable( GL_STENCIL_TEST );
        }
    }


    void Viewport::_drawCropMask(
        const imaging::Size& renderSize ) const noexcept
    {
        TLRENDER_GL();

        double aspectY = (double) renderSize.w / (double) renderSize.h;
        double aspectX = (double) renderSize.h / (double) renderSize.w;

        double target_aspect = 1.0 / _p->masking;
        double amountY = (0.5 - target_aspect * aspectY / 2);
        double amountX = (0.5 - _p->masking * aspectX / 2);

        bool vertical = true;
        if ( amountY < amountX )
        {
            vertical = false;
        }

        imaging::Color4f maskColor( 0, 0, 0, 1 );

        if ( vertical )
        {
            int Y = renderSize.h * amountY;
            math::BBox2i box( 0, 0, renderSize.w, Y );
            gl.render->drawRect( box, maskColor );
            box.max.y = renderSize.h;
            box.min.y = renderSize.h - Y;
            gl.render->drawRect( box, maskColor );
        }
        else
        {
            int X = renderSize.w * amountX;
            math::BBox2i box( 0, 0, X, renderSize.h );
            gl.render->drawRect( box, maskColor );
            box.max.x = renderSize.w;
            box.min.x = renderSize.w - X;
            gl.render->drawRect( box, maskColor );
        }

    }

    inline
    void Viewport::_drawText(
        const std::vector<std::shared_ptr<imaging::Glyph> >& glyphs,
        math::Vector2i& pos, const int16_t lineHeight,
        const imaging::Color4f& labelColor) const noexcept
    {
        TLRENDER_GL();
        const imaging::Color4f shadowColor(0.F, 0.F, 0.F, 0.7F);
        math::Vector2i shadowPos{ pos.x + 2, pos.y + 2 };
        gl.render->drawText( glyphs, shadowPos, shadowColor );
        gl.render->drawText( glyphs, pos, labelColor );
        pos.y += lineHeight;
    }

    void Viewport::_getPixelValue(
        imaging::Color4f& rgba,
        const std::shared_ptr<imaging::Image>& image,
        const math::Vector2i& pos ) const
    {
        TLRENDER_P();
        imaging::PixelType type = image->getPixelType();
        uint8_t channels = imaging::getChannelCount(type);
        uint8_t depth    = imaging::getBitDepth(type) / 8;
        const auto& info   = image->getInfo();
        imaging::VideoLevels  videoLevels = info.videoLevels;
        const math::Vector4f& yuvCoefficients =
            getYUVCoefficients( info.yuvCoefficients );
        imaging::Size size = image->getSize();
        const uint8_t*  data = image->getData();
        int X = pos.x;
        int Y = size.h - pos.y - 1;
        if ( p.displayOptions[0].mirror.x ) X = size.w - X - 1;
        if ( p.displayOptions[0].mirror.y ) Y = size.h - Y - 1;

        // Do some sanity check just in case
        if ( X < 0 || Y < 0 || X >= size.w || Y >= size.h )
            return;

        size_t offset = ( Y * size.w + X ) * depth;

        switch( type )
        {
        case imaging::PixelType::YUV_420P_U8:
        case imaging::PixelType::YUV_422P_U8:
        case imaging::PixelType::YUV_444P_U8:
            break;
        case imaging::PixelType::YUV_420P_U16:
        case imaging::PixelType::YUV_422P_U16:
        case imaging::PixelType::YUV_444P_U16:
            break;
        default:
            offset *= channels;
            break;
        }

        rgba.a = 1.0f;
        switch ( type )
        {
        case imaging::PixelType::L_U8:
            rgba.r = data[offset] / 255.0f;
            rgba.g = data[offset] / 255.0f;
            rgba.b = data[offset] / 255.0f;
            break;
        case imaging::PixelType::LA_U8:
            rgba.r = data[offset]   / 255.0f;
            rgba.g = data[offset]   / 255.0f;
            rgba.b = data[offset]   / 255.0f;
            rgba.a = data[offset+1] / 255.0f;
            break;
        case imaging::PixelType::L_U16:
        {
            uint16_t* f = (uint16_t*) (&data[offset]);
            rgba.r = f[0] / 65535.0f;
            rgba.g = f[0] / 65535.0f;
            rgba.b = f[0] / 65535.0f;
            break;
        }
        case imaging::PixelType::LA_U16:
        {
            uint16_t* f = (uint16_t*) (&data[offset]);
            rgba.r = f[0] / 65535.0f;
            rgba.g = f[0] / 65535.0f;
            rgba.b = f[0] / 65535.0f;
            rgba.a = f[1] / 65535.0f;
            break;
        }
        case imaging::PixelType::L_U32:
        {
            uint32_t* f = (uint32_t*) (&data[offset]);
            constexpr float max = static_cast<float>(
                std::numeric_limits<uint32_t>::max() );
            rgba.r = f[0] / max;
            rgba.g = f[0] / max;
            rgba.b = f[0] / max;
            break;
        }
        case imaging::PixelType::LA_U32:
        {
            uint32_t* f = (uint32_t*) (&data[offset]);
            constexpr float max = static_cast<float>(
                std::numeric_limits<uint32_t>::max() );
            rgba.r = f[0] / max;
            rgba.g = f[0] / max;
            rgba.b = f[0] / max;
            rgba.a = f[1] / max;
            break;
        }
        case imaging::PixelType::L_F16:
        {
            half* f = (half*) (&data[offset]);
            rgba.r = f[0];
            rgba.g = f[0];
            rgba.b = f[0];
            break;
        }
        case imaging::PixelType::LA_F16:
        {
            half* f = (half*) (&data[offset]);
            rgba.r = f[0];
            rgba.g = f[0];
            rgba.b = f[0];
            rgba.a = f[1];
            break;
        }
        case imaging::PixelType::RGB_U8:
            rgba.r = data[offset] / 255.0f;
            rgba.g = data[offset+1] / 255.0f;
            rgba.b = data[offset+2] / 255.0f;
            break;
        case imaging::PixelType::RGB_U10:
        {
            imaging::U10* f = (imaging::U10*) (&data[offset]);
            constexpr float max = static_cast<float>(
                std::numeric_limits<uint32_t>::max() );
            rgba.r = f->r / max;
            rgba.g = f->g / max;
            rgba.b = f->b / max;
            break;
        }
        case imaging::PixelType::RGBA_U8:
            rgba.r = data[offset] / 255.0f;
            rgba.g = data[offset+1] / 255.0f;
            rgba.b = data[offset+2] / 255.0f;
            rgba.a = data[offset+3] / 255.0f;
            break;
        case imaging::PixelType::RGB_U16:
        {
            uint16_t* f = (uint16_t*) (&data[offset]);
            rgba.r = f[0] / 65535.0f;
            rgba.g = f[1] / 65535.0f;
            rgba.b = f[2] / 65535.0f;
            break;
        }
        case imaging::PixelType::RGBA_U16:
        {
            uint16_t* f = (uint16_t*) (&data[offset]);
            rgba.r = f[0] / 65535.0f;
            rgba.g = f[1] / 65535.0f;
            rgba.b = f[2] / 65535.0f;
            rgba.a = f[3] / 65535.0f;
            break;
        }
        case imaging::PixelType::RGB_U32:
        {
            uint32_t* f = (uint32_t*) (&data[offset]);
            constexpr float max = static_cast<float>(
                std::numeric_limits<uint32_t>::max() );
            rgba.r = f[0] / max;
            rgba.g = f[1] / max;
            rgba.b = f[2] / max;
            break;
        }
        case imaging::PixelType::RGBA_U32:
        {
            uint32_t* f = (uint32_t*) (&data[offset]);
            constexpr float max = static_cast<float>(
                std::numeric_limits<uint32_t>::max() );
            rgba.r = f[0] / max;
            rgba.g = f[1] / max;
            rgba.b = f[2] / max;
            rgba.a = f[3] / max;
            break;
        }
        case imaging::PixelType::RGB_F16:
        {
            half* f = (half*) (&data[offset]);
            rgba.r = f[0];
            rgba.g = f[1];
            rgba.b = f[2];
            break;
        }
        case imaging::PixelType::RGBA_F16:
        {
            half* f = (half*) (&data[offset]);
            rgba.r = f[0];
            rgba.g = f[1];
            rgba.b = f[2];
            rgba.a = f[3];
            break;
        }
        case imaging::PixelType::RGB_F32:
        {
            float* f = (float*) (&data[offset]);
            rgba.r = f[0];
            rgba.g = f[1];
            rgba.b = f[2];
            break;
        }
        case imaging::PixelType::RGBA_F32:
        {
            float* f = (float*) (&data[offset]);
            rgba.r = f[0];
            rgba.g = f[1];
            rgba.b = f[2];
            rgba.a = f[3];
            break;
        }
        case imaging::PixelType::YUV_420P_U8:
        {
            size_t Ysize = size.w * size.h;
            size_t w2      = (size.w + 1) / 2;
            size_t h2      = (size.h + 1) / 2;
            size_t Usize   = w2 * h2;
            size_t offset2 = (Y/2) * w2 + X / 2;
            rgba.r = data[ offset ]                  / 255.0f;
            rgba.g = data[ Ysize + offset2 ]         / 255.0f;
            rgba.b = data[ Ysize + Usize + offset2 ] / 255.0f;
            color::checkLevels( rgba, videoLevels );
            rgba = color::YPbPr::to_rgb( rgba, yuvCoefficients );
            break;
        }
        case imaging::PixelType::YUV_422P_U8:
        {
            size_t Ysize = size.w * size.h;
            size_t w2      = (size.w + 1) / 2;
            size_t Usize   = w2 * size.h;
            size_t offset2 = Y * w2 + X / 2;
            rgba.r = data[ offset ]              / 255.0f;
            rgba.g = data[ Ysize + offset2 ]         / 255.0f;
            rgba.b = data[ Ysize + Usize + offset2 ] / 255.0f;
            color::checkLevels( rgba, videoLevels );
            rgba = color::YPbPr::to_rgb( rgba, yuvCoefficients );
            break;
        }
        case imaging::PixelType::YUV_444P_U8:
        {
            size_t Ysize = size.w * size.h;
            rgba.r = data[ offset ]             / 255.0f;
            rgba.g = data[ Ysize + offset ]     / 255.0f;
            rgba.b = data[ Ysize * 2 + offset ] / 255.0f;
            color::checkLevels( rgba, videoLevels );
            rgba = color::YPbPr::to_rgb( rgba, yuvCoefficients );
            break;
        }
        case imaging::PixelType::YUV_420P_U16:
        {
            size_t pos = Y * size.w / 4 + X / 2;
            size_t Ysize = size.w * size.h;
            size_t Usize = Ysize / 4;
            rgba.r = data[ offset ]              / 65535.0f;
            rgba.g = data[ Ysize + pos ]         / 65535.0f;
            rgba.b = data[ Ysize + Usize + pos ] / 65535.0f;
            color::checkLevels( rgba, videoLevels );
            rgba = color::YPbPr::to_rgb( rgba, yuvCoefficients );
            break;
        }
        case imaging::PixelType::YUV_422P_U16:
        {
            size_t Ysize = size.w * size.h * depth;
            size_t pos = Y * size.w + X;
            size_t Usize = size.w / 2 * size.h * depth;
            rgba.r = data[ offset ]              / 65535.0f;
            rgba.g = data[ Ysize + pos ]         / 65535.0f;
            rgba.b = data[ Ysize + Usize + pos ] / 65535.0f;
            color::checkLevels( rgba, videoLevels );
            rgba = color::YPbPr::to_rgb( rgba, yuvCoefficients );
            break;
        }
        case imaging::PixelType::YUV_444P_U16:
        {
            size_t Ysize = size.w * size.h * depth;
            rgba.r = data[ offset ]             / 65535.0f;
            rgba.g = data[ Ysize + offset ]     / 65535.0f;
            rgba.b = data[ Ysize * 2 + offset ] / 65535.0f;
            color::checkLevels( rgba, videoLevels );
            rgba = color::YPbPr::to_rgb( rgba, yuvCoefficients );
            break;
        }
        default:
            break;
        }

    }

    void Viewport::_bindReadImage()
    {
        TLRENDER_P();
        TLRENDER_GL();

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE );

        const GLenum format = GL_BGRA;  // for faster access, we muse use BGRA.
        const GLenum type = GL_FLOAT;
        const imaging::Size& renderSize = gl.buffer->getSize();

        // set the target framebuffer to read
        gl::OffscreenBufferBinding binding(gl.buffer);
        // "index" is used to read pixels from framebuffer to a PBO
        // "nextIndex" is used to update pixels in the other PBO
        gl.index = (gl.index + 1) % 2;
        gl.nextIndex = (gl.index + 1) % 2;

        // read pixels from framebuffer to PBO
        // glReadPixels() should return immediately.
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gl.pboIds[gl.index]);
        glReadPixels(0, 0, renderSize.w, renderSize.h, format, type, 0);

        // map the PBO to process its data by CPU
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gl.pboIds[gl.nextIndex]);
        gl.image = (GLfloat*)glMapBuffer(GL_PIXEL_PACK_BUFFER,
                                             GL_READ_ONLY);
    }

    void Viewport::_calculateColorArea( mrv::area::Info& info )
    {
        TLRENDER_P();
        TLRENDER_GL();

        if( !gl.image ) return;
            
        BrightnessType brightness_type =
            (BrightnessType) p.ui->uiLType->value();
        info.rgba.max.r = std::numeric_limits<float>::min();
        info.rgba.max.g = std::numeric_limits<float>::min();
        info.rgba.max.b = std::numeric_limits<float>::min();
        info.rgba.max.a = std::numeric_limits<float>::min();

        info.rgba.min.r = std::numeric_limits<float>::max();
        info.rgba.min.g = std::numeric_limits<float>::max();
        info.rgba.min.b = std::numeric_limits<float>::max();
        info.rgba.min.a = std::numeric_limits<float>::max();

        info.rgba.mean.r = info.rgba.mean.g = info.rgba.mean.b =
        info.rgba.mean.a = 0.F;


        info.hsv.max.r = std::numeric_limits<float>::min();
        info.hsv.max.g = std::numeric_limits<float>::min();
        info.hsv.max.b = std::numeric_limits<float>::min();
        info.hsv.max.a = std::numeric_limits<float>::min();

        info.hsv.min.r = std::numeric_limits<float>::max();
        info.hsv.min.g = std::numeric_limits<float>::max();
        info.hsv.min.b = std::numeric_limits<float>::max();
        info.hsv.min.a = std::numeric_limits<float>::max();

        info.hsv.mean.r = info.hsv.mean.g = info.hsv.mean.b =
        info.hsv.mean.a = 0.F;

        int hsv_colorspace = p.ui->uiBColorType->value() + 1;

        int maxX = info.box.max.x;
        int maxY = info.box.max.y;
        const auto& renderSize = gl.buffer->getSize();
        for ( int Y = info.box.y(); Y < maxY; ++Y )
        {
            for ( int X = info.box.x(); X < maxX; ++X )
            {
                imaging::Color4f rgba, hsv;
                rgba.b = gl.image[ ( X + Y * renderSize.w ) * 4 ];
                rgba.g = gl.image[ ( X + Y * renderSize.w ) * 4 + 1 ];
                rgba.r = gl.image[ ( X + Y * renderSize.w ) * 4 + 2 ];
                rgba.a = gl.image[ ( X + Y * renderSize.w ) * 4 + 3 ];


                info.rgba.mean.r += rgba.r;
                info.rgba.mean.g += rgba.g;
                info.rgba.mean.b += rgba.b;
                info.rgba.mean.a += rgba.a;

                if ( rgba.r < info.rgba.min.r ) info.rgba.min.r = rgba.r;
                if ( rgba.g < info.rgba.min.g ) info.rgba.min.g = rgba.g;
                if ( rgba.b < info.rgba.min.b ) info.rgba.min.b = rgba.b;
                if ( rgba.a < info.rgba.min.a ) info.rgba.min.a = rgba.a;

                if ( rgba.r > info.rgba.max.r ) info.rgba.max.r = rgba.r;
                if ( rgba.g > info.rgba.max.g ) info.rgba.max.g = rgba.g;
                if ( rgba.b > info.rgba.max.b ) info.rgba.max.b = rgba.b;
                if ( rgba.a > info.rgba.max.a ) info.rgba.max.a = rgba.a;

                if ( rgba.r < 0 ) rgba.r = 0.F;
                if ( rgba.g < 0 ) rgba.g = 0.F;
                if ( rgba.b < 0 ) rgba.b = 0.F;
                if ( rgba.r > 1 ) rgba.r = 1.F;
                if ( rgba.g > 1 ) rgba.g = 1.F;
                if ( rgba.b > 1 ) rgba.b = 1.F;

                switch( hsv_colorspace )
                {
                case color::kHSV:
                    hsv = color::rgb::to_hsv( rgba );
                    break;
                case color::kHSL:
                    hsv = color::rgb::to_hsl( rgba );
                    break;
                case color::kCIE_XYZ:
                    hsv = color::rgb::to_xyz( rgba );
                    break;
                case color::kCIE_xyY:
                    hsv = color::rgb::to_xyY( rgba );
                    break;
                case color::kCIE_Lab:
                    hsv = color::rgb::to_lab( rgba );
                    break;
                case color::kCIE_Luv:
                    hsv = color::rgb::to_luv( rgba );
                    break;
                case color::kYUV:
                    hsv = color::rgb::to_yuv( rgba );
                    break;
                case color::kYDbDr:
                    hsv = color::rgb::to_YDbDr( rgba );
                    break;
                case color::kYIQ:
                    hsv = color::rgb::to_yiq( rgba );
                    break;
                case color::kITU_601:
                    hsv = color::rgb::to_ITU601( rgba );
                    break;
                case color::kITU_709:
                    hsv = color::rgb::to_ITU709( rgba );
                    break;
                case color::kRGB:
                default:
                    hsv = rgba;
                    break;
                }
                hsv.a = calculate_brightness( rgba, brightness_type );

                info.hsv.mean.r += hsv.r;
                info.hsv.mean.g += hsv.g;
                info.hsv.mean.b += hsv.b;
                info.hsv.mean.a += hsv.a;

                if ( hsv.r < info.hsv.min.r ) info.hsv.min.r = hsv.r;
                if ( hsv.g < info.hsv.min.g ) info.hsv.min.g = hsv.g;
                if ( hsv.b < info.hsv.min.b ) info.hsv.min.b = hsv.b;
                if ( hsv.a < info.hsv.min.a ) info.hsv.min.a = hsv.a;

                if ( hsv.r > info.hsv.max.r ) info.hsv.max.r = hsv.r;
                if ( hsv.g > info.hsv.max.g ) info.hsv.max.g = hsv.g;
                if ( hsv.b > info.hsv.max.b ) info.hsv.max.b = hsv.b;
                if ( hsv.a > info.hsv.max.a ) info.hsv.max.a = hsv.a;
            }
        }

        int num = info.box.w() * info.box.h();
        info.rgba.mean.r /= num;
        info.rgba.mean.g /= num;
        info.rgba.mean.b /= num;
        info.rgba.mean.a /= num;

        info.rgba.diff.r = info.rgba.max.r - info.rgba.min.r;
        info.rgba.diff.g = info.rgba.max.g - info.rgba.min.g;
        info.rgba.diff.b = info.rgba.max.b - info.rgba.min.b;
        info.rgba.diff.a = info.rgba.max.a - info.rgba.min.a;

        info.hsv.mean.r /= num;
        info.hsv.mean.g /= num;
        info.hsv.mean.b /= num;
        info.hsv.mean.a /= num;

        info.hsv.diff.r = info.hsv.max.r - info.hsv.min.r;
        info.hsv.diff.g = info.hsv.max.g - info.hsv.min.g;
        info.hsv.diff.b = info.hsv.max.b - info.hsv.min.b;
        info.hsv.diff.a = info.hsv.max.a - info.hsv.min.a;
    }


    void Viewport::_readPixel( imaging::Color4f& rgba ) const noexcept
    {
        // If window was not yet mapped, return immediately
        if ( !valid() ) return;

        TLRENDER_P();
        TLRENDER_GL();

        math::Vector2i pos;
        pos.x = ( p.mousePos.x - p.viewPos.x ) / p.viewZoom;
        pos.y = ( p.mousePos.y - p.viewPos.y ) / p.viewZoom;

        if ( p.ui->uiPixelValue->value() != PixelValue::kFull )
        {

            rgba.r = rgba.g = rgba.b = rgba.a = 0.f;

            for ( const auto& video : p.videoData )
            {
                for ( const auto& layer : video.layers )
                {
                    const auto& image = layer.image;
                    if ( ! image->isValid() ) continue;

                    imaging::Color4f pixel, pixelB;

                    _getPixelValue( pixel, image, pos );


                    const auto& imageB = layer.image;
                    if ( imageB->isValid() )
                    {
                        _getPixelValue( pixelB, imageB, pos );

                        if ( layer.transition ==
                             timeline::Transition::Dissolve )
                        {
                            float f2 = layer.transitionValue;
                            float  f = 1.0 - f2;
                            pixel.r = pixel.r * f + pixelB.r * f2;
                            pixel.g = pixel.g * f + pixelB.g * f2;
                            pixel.b = pixel.b * f + pixelB.b * f2;
                            pixel.a = pixel.a * f + pixelB.a * f2;
                        }
                    }
                    rgba.r += pixel.r;
                    rgba.g += pixel.g;
                    rgba.b += pixel.b;
                    rgba.a += pixel.a;
                }
            }
        }
        else
        {
            // This is needed as the FL_MOVE of fltk wouuld get called
            // before the draw routine
            if ( !gl.buffer ) return;

            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE );

            gl::OffscreenBufferBinding binding(gl.buffer);


            const GLenum format = GL_BGRA;  // for faster access, we muse use BGRA.
            const GLenum type = GL_FLOAT;

            // We use ReadPixels when the movie is stopped or has only a
            // a single frame.
            bool update = false;
            if ( !p.timelinePlayers.empty() )
            {
                auto player = p.timelinePlayers[0];
                update = ( player->playback() == timeline::Playback::Stop );
                if ( player->inOutRange().duration().to_frames() )
                    update = true;
            }
            if ( update )
            {
                glReadPixels( pos.x, pos.y, 1, 1, GL_RGBA, type, &rgba);
                return;
            }



            const imaging::Size& renderSize = gl.buffer->getSize();

            if ( ! gl.image )
            {
                // set the target framebuffer to read
                // "index" is used to read pixels from framebuffer to a PBO
                // "nextIndex" is used to update pixels in the other PBO
                gl.index = (gl.index + 1) % 2;
                gl.nextIndex = (gl.index + 1) % 2;

                // read pixels from framebuffer to PBO
                // glReadPixels() should return immediately.
                glBindBuffer(GL_PIXEL_PACK_BUFFER, gl.pboIds[gl.index]);
                glReadPixels(0, 0, renderSize.w, renderSize.h, format, type, 0);

                // map the PBO to process its data by CPU
                glBindBuffer(GL_PIXEL_PACK_BUFFER, gl.pboIds[gl.nextIndex]);

                gl.image = (GLfloat*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            }

            if( gl.image )
            {
                rgba.b = gl.image[ ( pos.x + pos.y * renderSize.w ) * 4 ];
                rgba.g = gl.image[ ( pos.x + pos.y * renderSize.w ) * 4 + 1 ];
                rgba.r = gl.image[ ( pos.x + pos.y * renderSize.w ) * 4 + 2 ];
                rgba.a = gl.image[ ( pos.x + pos.y * renderSize.w ) * 4 + 3 ];
            }
        }

        if ( gl.image )
        {
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            gl.image = nullptr;
        }

        // back to conventional pixel operations
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    }

    bool Viewport::getHudActive() const
    {
        return _p->hudActive;
    }

    void Viewport::setHudActive( const bool active )
    {
        _p->hudActive = active;
        redraw();
    }

    void Viewport::setHudDisplay( const HudDisplay hud )
    {
        _p->hud = hud;
        redraw();
    }

    HudDisplay Viewport::getHudDisplay() const noexcept
    {
        return _p->hud;
    }

    void Viewport::_drawSafeAreas(
        const float percentX, const float percentY,
        const float pixelAspectRatio,
        const imaging::Color4f& color,
        const math::Matrix4x4f& mvp,
        const char* label ) const noexcept
    {
        TLRENDER_GL();
        const auto& renderSize = getRenderSize();
        double aspectX = (double) renderSize.h / (double) renderSize.w;
        double aspectY = (double) renderSize.w / (double) renderSize.h;

        double amountY = (0.5 - percentY * aspectY / 2);
        double amountX = (0.5 - percentX * aspectX / 2);

        bool vertical = true;
        if ( amountY < amountX )
        {
            vertical = false;
        }

        math::BBox2i box;
        int X, Y;
        if ( vertical )
        {
            X = renderSize.w * percentX;
            Y = renderSize.h * amountY;
        }
        else
        {
          X = renderSize.w * amountX / pixelAspectRatio;
          Y = renderSize.h * percentY;
        }
        box.min.x = renderSize.w - X;
        box.min.y = -(renderSize.h - Y);
        box.max.x = X;
        box.max.y = -Y;
        _drawRectangleOutline( box, color, mvp );

        //
        // Draw the text too
        //
        static const std::string fontFamily = "NotoSans-Regular";
        Viewport* self = const_cast< Viewport* >( this );
        const imaging::FontInfo fontInfo(fontFamily, 12 * self->pixels_per_unit());
        const auto glyphs = _p->fontSystem->getGlyphs( label, fontInfo );
        math::Vector2i pos( box.max.x, box.max.y );
        // Set the projection matrix
        gl.render->setMatrix( mvp );
        gl.render->drawText( glyphs, pos, color );
    }

    void Viewport::_drawSafeAreas() const noexcept
    {
        TLRENDER_P();
        if ( p.timelinePlayers.empty() ) return;
        const auto& player = p.timelinePlayers[0];
        const auto& info   = player->timelinePlayer()->getIOInfo();
        const auto& video  = info.video[0];
        const auto pr      = video.size.pixelAspectRatio;

        const auto& viewportSize = getViewportSize();
        const auto& renderSize = getRenderSize();

        glm::mat4x4 vm(1.F);
        vm = glm::translate(vm, glm::vec3(p.viewPos.x,
                                          p.viewPos.y, 0.F));
        vm = glm::scale(vm, glm::vec3(p.viewZoom, p.viewZoom, 1.F));
        glm::mat4x4 pm = glm::ortho(
            0.F,
            static_cast<float>(viewportSize.w),
            0.F,
            static_cast<float>(viewportSize.h),
            -1.F,
            1.F);
        glm::mat4x4 vpm = pm * vm;
        vpm = glm::scale(vpm, glm::vec3(1.F, -1.F, 1.F));
        auto mvp = math::Matrix4x4f(
            vpm[0][0], vpm[0][1], vpm[0][2], vpm[0][3],
            vpm[1][0], vpm[1][1], vpm[1][2], vpm[1][3],
            vpm[2][0], vpm[2][1], vpm[2][2], vpm[2][3],
            vpm[3][0], vpm[3][1], vpm[3][2], vpm[3][3] );

        double aspectY = (double) renderSize.w / (double) renderSize.h;
        if ( aspectY < 1.66 || (aspectY >= 1.77 && aspectY <= 1.78) )
        {
            imaging::Color4f color( 1.F, 0.F, 0.F );
            _drawSafeAreas( 0.9F, 0.9F, pr, color, mvp, N_("tv action") );
            _drawSafeAreas( 0.8F, 0.8F, pr, color, mvp, N_("tv title") );
        }
        else
        {
            imaging::Color4f color( 1.F, 0.F, 0.F );
            if ( pr == 1.F )
            {
                // Assume film, draw 2.35, 1.85, 1.66 and hdtv areas
                _drawSafeAreas( 2.35, 1.F, pr, color, mvp, _("2.35") );
                color = imaging::Color4f( 1.F, 1.0f, 0.F );
                _drawSafeAreas( 1.89, 1.F, pr, color, mvp, _("1.85") );
                color = imaging::Color4f( 0.F, 1.0f, 1.F );
                _drawSafeAreas( 1.66, 1.F, pr, color, mvp, _("1.66") );
                // Draw hdtv too
                color = imaging::Color4f( 1.F, 0.0f, 1.F );
                _drawSafeAreas( 1.77, 1.0, pr, color, mvp, N_("hdtv") );
            }
            else
            {
              float f = 1.33F;
              // Film fit for TV, Draw 4-3 safe areas
              _drawSafeAreas( f*0.9F, 0.9F, pr, color, mvp, N_("tv action") );
              _drawSafeAreas( f*0.8F, 0.8F, pr, color, mvp, N_("tv title") );
            }
        }
    }

    int Viewport::handle( int event )
    {
        TLRENDER_GL();
        TLRENDER_P();
        int ok = TimelineViewport::handle( event );
        if ( event == FL_HIDE )
        {
            gl.render.reset();
            gl.buffer.reset();
            gl.shader.reset();
            gl.vbo.reset();
            gl.vao.reset();
            glDeleteBuffers(2, gl.pboIds);
            gl.pboIds[0] = gl.pboIds[1] = 0;
            p.fontSystem.reset();
            valid(0);
            context_valid(0);
        }
        return ok;
    }

    void Viewport::_drawHUD() const noexcept
    {
        TLRENDER_P();
        TLRENDER_GL();

        Viewport* self = const_cast< Viewport* >( this );

        const auto& viewportSize = getViewportSize();

        timeline::RenderOptions renderOptions;
        renderOptions.clear = false;
        gl.render->begin( viewportSize, renderOptions );

        std::string fontFamily = "NotoSans-Regular";
        uint16_t fontSize = 12 * self->pixels_per_unit();

        Fl_Color c = p.ui->uiPrefs->uiPrefsViewHud->color();
        uint8_t r, g, b;
        Fl::get_color( c, r, g, b );

        const imaging::Color4f labelColor(r / 255.F, g / 255.F, b / 255.F);

        char buf[128];
        const imaging::FontInfo fontInfo(fontFamily, fontSize);
        const imaging::FontMetrics fontMetrics =
            p.fontSystem->getMetrics(fontInfo);
        auto lineHeight = fontMetrics.lineHeight;
        math::Vector2i pos( 20, lineHeight*2 );

        const auto& player = p.timelinePlayers[0];
        const auto& path   = player->path();
        const otime::RationalTime& time = player->currentTime();
        int64_t frame = time.to_frames();

        const auto& directory = path.getDirectory();

        std::string fullname = createStringFromPathAndTime( path, time );

        if ( p.hud & HudDisplay::kDirectory )
            _drawText( p.fontSystem->getGlyphs(directory, fontInfo),
                       pos, lineHeight, labelColor );

        if ( p.hud & HudDisplay::kFilename )
            _drawText( p.fontSystem->getGlyphs(fullname, fontInfo), pos,
                       lineHeight, labelColor );

        if ( p.hud & HudDisplay::kResolution )
        {
            const auto& info   = player->timelinePlayer()->getIOInfo();
            const auto& video = info.video[0];
            if ( video.size.pixelAspectRatio != 1.0 )
            {
                int width = video.size.w * video.size.pixelAspectRatio;
                sprintf( buf, "%d x %d  ( %.3g )  %d x %d",
                         video.size.w, video.size.h,
                         video.size.pixelAspectRatio, width, video.size.h);
            }
            else
            {
                sprintf( buf, "%d x %d", video.size.w, video.size.h );
            }
            _drawText( p.fontSystem->getGlyphs(buf, fontInfo), pos,
                       lineHeight, labelColor );
        }

        const otime::TimeRange&    range = player->timeRange();
        const otime::RationalTime& duration = range.end_time_inclusive() -
                                              range.start_time();

        std::string tmp;
        if ( p.hud & HudDisplay::kFrame )
        {
            sprintf( buf, "F: %" PRId64 " ", frame );
            tmp += buf;
        }

        if ( p.hud & HudDisplay::kFrameRange )
        {
            const auto& range = player->timeRange();
            frame = range.start_time().to_frames();
            const int64_t last_frame = range.end_time_inclusive().to_frames();
            sprintf( buf, "Range: %" PRId64 " -  %" PRId64,
                     frame, last_frame );
            tmp += buf;
        }

        if ( p.hud & HudDisplay::kTimecode )
        {
          sprintf( buf, "TC: %s ", time.to_timecode(nullptr).c_str() );
          tmp += buf;
        }

        if ( p.hud & HudDisplay::kFPS )
        {
            sprintf( buf, "FPS: %.3f", p.ui->uiFPS->value() );
            tmp += buf;
        }

        if ( !tmp.empty() )
            _drawText( p.fontSystem->getGlyphs(tmp, fontInfo), pos,
                       lineHeight, labelColor );

        tmp.clear();
        if ( p.hud & HudDisplay::kFrameCount )
        {
            sprintf( buf, "FC: %" PRId64, (int64_t)duration.value() );
            tmp += buf;
        }


        if ( !tmp.empty() )
            _drawText( p.fontSystem->getGlyphs(tmp, fontInfo), pos,
                       lineHeight, labelColor );

        if ( p.hud & HudDisplay::kAttributes )
        {
            const auto& info   = player->timelinePlayer()->getIOInfo();
            for ( auto& tag : info.tags )
            {
                sprintf( buf, "%s = %s",
                         tag.first.c_str(), tag.second.c_str() );
                _drawText( p.fontSystem->getGlyphs(buf, fontInfo), pos,
                           lineHeight, labelColor );
            }
        }

    }
}
