//
// Created by consti10 on 15.09.22.
//
#include <GL/gl.h>
#include <chrono>

#include "app/videostreaming/avcodec/color_helper.h"
#include "gl_videorenderer.h"


static EGLint texgen_attrs[] = {
	EGL_DMA_BUF_PLANE0_FD_EXT,
	EGL_DMA_BUF_PLANE0_OFFSET_EXT,
	EGL_DMA_BUF_PLANE0_PITCH_EXT,
	EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
	EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
	EGL_DMA_BUF_PLANE1_FD_EXT,
	EGL_DMA_BUF_PLANE1_OFFSET_EXT,
	EGL_DMA_BUF_PLANE1_PITCH_EXT,
	EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
	EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
	EGL_DMA_BUF_PLANE2_FD_EXT,
	EGL_DMA_BUF_PLANE2_OFFSET_EXT,
	EGL_DMA_BUF_PLANE2_PITCH_EXT,
	EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
	EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
};

static void create_rgba_texture(GLuint& tex_id, int32_t color_rgba) {
    assert(tex_id==0);
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const int width = 1280;
    const int height = 720;
    uint8_t pixels[4*width*height];
    fillFrame(pixels, width, height, width*4, color_rgba);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GL_VideoRenderer::init_gl() {
    create_rgba_texture(texture_rgb_green, create_pixel_rgba(0, 255, 0, 255));
    create_rgba_texture(texture_rgb_blue, create_pixel_rgba(0, 0, 255, 255));
    gl_shaders = std::make_unique<GL_shaders>();
    gl_shaders->initialize();
}

// https://stackoverflow.com/questions/9413845/ffmpeg-avframe-to-opengl-texture-without-yuv-to-rgb-soft-conversion
// https://bugfreeblog.duckdns.org/2022/01/yuv420p-opengl-shader-conversion.html
// https://stackoverflow.com/questions/30191911/is-it-possible-to-draw-yuv422-and-yuv420-texture-using-opengl
void GL_VideoRenderer::update_texture_yuv420P_yuv422P(AVFrame* frame) {
    assert(frame != nullptr);
    assert(is_AV_PIX_FMT_YUV42XP(frame->format));
    const GLuint frame_width = frame->width;
    const GLuint frame_height = frame->height;
    // Both 420 and 422 have half width
    const GLuint uv_width = frame_width/2;
    // 420 has half height, 422 has full height
    const GLuint uv_height = is_AV_PIX_FMT_YUV420P(frame->format) ? frame_height/2 : frame_height;
    GLuint widths[3] = {
        frame_width,
        uv_width,
        uv_width
    };
    GLuint heights[3] = {
        frame_height,
        uv_height,
        uv_height
    };
    // Better be safe than store with how annying QT can be
    int gl_unpack_row_length_before = 0;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &gl_unpack_row_length_before);
    int gl_unpack_alignment_before = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &gl_unpack_alignment_before);
    for (int i = 0; i < 3; i++) {
        if (yuv_420_p_sw_frame_texture.textures[i] == 0) {
            glGenTextures(1, &yuv_420_p_sw_frame_texture.textures[i]);
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, yuv_420_p_sw_frame_texture.textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, widths[i], heights[i], 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[i]);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, gl_unpack_row_length_before);
    glPixelStorei(GL_UNPACK_ALIGNMENT, gl_unpack_alignment_before);
    glBindTexture(GL_TEXTURE_2D, 0);
    GL_shaders::checkGlError("upload YUV420P");
    yuv_420_p_sw_frame_texture.has_valid_image = true;
    GL_shaders::checkGlError("upload YUV420P");
}

void GL_VideoRenderer::update_texture_nv12(AVFrame* frame) {
    assert(frame != nullptr);
    assert(is_AV_PIX_FMT_NV12(frame->format));
    const GLuint frame_width = frame->width;
    const GLuint frame_height = frame->height;
    const GLuint uv_width = frame_width/2;
    const GLuint uv_height = frame_height/2;
    GLuint widths[2] = {
        frame_width,
        uv_width,
    };
    GLuint heights[2] = {
        frame_height,
        uv_height,
    };

    // Better be safe than store with how annying QT can be
    int gl_unpack_row_length_before = 0;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &gl_unpack_row_length_before);
    int gl_unpack_alignment_before = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &gl_unpack_alignment_before);

    for (int i = 0; i < 2; i++) {
        if (nv12_frametexture.textures[i] == 0) {
            glGenTextures(1, &nv12_frametexture.textures[i]);
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, nv12_frametexture.textures[i]);
        if (i == 1) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[i]/2);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, widths[i], heights[i], 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, frame->data[i]);
        } else {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, widths[i], heights[i], 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[i]);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, gl_unpack_row_length_before);
    glPixelStorei(GL_UNPACK_ALIGNMENT, gl_unpack_alignment_before);
    glBindTexture(GL_TEXTURE_2D, 0);

    GL_shaders::checkGlError("upload NV12");
    nv12_frametexture.has_valid_image = true;
    GL_shaders::checkGlError("upload NV12");
}

// Also https://code.videolan.org/videolan/vlc/-/blob/master/modules/video_output/opengl/importer.c#L414-417
bool GL_VideoRenderer::update_texture_egl_external(AVFrame* frame) {
    assert(frame);
    assert(frame->format==AV_PIX_FMT_DRM_PRIME);
    EGLDisplay egl_display=eglGetCurrentDisplay();
    assert(egl_display);
    // We can now also give the frame back to av, since we are updating to a new one.
    if(egl_frame_texture.av_frame != nullptr){
        av_frame_free(&egl_frame_texture.av_frame);
    }
    egl_frame_texture.av_frame = frame;
    const AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor*)frame->data[0];
    // Writing all the EGL attribs - I just copied and pasted it, and it works.
    EGLint attribs[50];
    EGLint * a = attribs;
    const EGLint * b = texgen_attrs;
    *a++ = EGL_WIDTH;
    *a++ = x_av_frame_cropped_width(frame);
    *a++ = EGL_HEIGHT;
    *a++ = x_av_frame_cropped_height(frame);
    *a++ = EGL_LINUX_DRM_FOURCC_EXT;
    *a++ = desc->layers[0].format;
    for (int i = 0; i < desc->nb_layers; ++i) {
        for (int j = 0; j < desc->layers[i].nb_planes; ++j) {
            const AVDRMPlaneDescriptor * const p = desc->layers[i].planes + j;
            const AVDRMObjectDescriptor * const obj = desc->objects + p->object_index;
            *a++ = *b++;
            *a++ = obj->fd;
            *a++ = *b++;
            *a++ = p->offset;
            *a++ = *b++;
            *a++ = p->pitch;
            if (obj->format_modifier == 0) {
            b += 2;
            }
            else {
            *a++ = *b++;
            *a++ = (EGLint)(obj->format_modifier & 0xFFFFFFFF);
            *a++ = *b++;
            *a++ = (EGLint)(obj->format_modifier >> 32);
            }
        }
    }
    *a = EGL_NONE;
    const EGLImage image = eglCreateImageKHR(egl_display,
                                           EGL_NO_CONTEXT,
                                           EGL_LINUX_DMA_BUF_EXT,
                                           NULL, attribs);
    if (image != nullptr) {
        std::cout << "Failed to create EGLImage " << strerror(errno) << std::endl;;
        egl_frame_texture.has_valid_image= false;
        std::cout << all_av_hwframe_transfer_formats(frame->hw_frames_ctx) << std::endl;
        return false;
    }
    // Note that we do not have to delete and generate the texture (ID) every time we update the egl image backing.
    if (egl_frame_texture.texture == 0) {
        glGenTextures(1, &egl_frame_texture.texture);
    }
    glEnable(GL_TEXTURE_EXTERNAL_OES);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, egl_frame_texture.texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
    // I do not know exactly how that works, but we seem to be able to immediately delete the EGL image, as long as we don't give the frame
    // back to the decoder I assume
    eglDestroyImageKHR(egl_display, image);
    egl_frame_texture.has_valid_image= true;
    return true;
}

// TODO r.n we have 2 CPU copies !
//https://registry.khronos.org/OpenGL/extensions/NV/NV_vdpau_interop.txt
void GL_VideoRenderer::update_texture_vdpau(AVFrame* hw_frame) {
    assert(hw_frame != nullptr);
    std::cerr << "Update_texture_vdpau:"<<safe_av_get_pix_fmt_name((AVPixelFormat)hw_frame->format) << "slow\n";
    std::cout << all_av_hwframe_transfer_formats(hw_frame->hw_frames_ctx);
    AVFrame* sw_frame = av_frame_alloc();
    assert(sw_frame);
    sw_frame->format=AV_PIX_FMT_YUV420P;
    if (av_hwframe_transfer_data(sw_frame, hw_frame, 0) != 0) {
        fprintf(stderr, "Error transferring the data to system memory\n");
        av_frame_free(&sw_frame);
        av_frame_free(&hw_frame);
        return;
    }
    update_texture_yuv420P_yuv422P(sw_frame);
    av_frame_free(&sw_frame);
}

// "Consumes" the given hw_frame (makes sure it is freed at the apropriate time / the previous one
// is freed when updating to a new one.
void GL_VideoRenderer::update_texture_gl(AVFrame *frame) {
    // We have a new frame and we update (one of the HW/SW) textures with it that will then
    // be rendered - make sure to discard anything that is still "on the screen" in case the
    // format has changed in between calls
    mark_all_video_textures_as_without();
    _current_video_width = frame->width;
    _current_video_height = frame->height;
    if (frame->format == AV_PIX_FMT_DRM_PRIME) {
        update_texture_egl_external(frame);
    } else if (is_AV_PIX_FMT_NV12(frame->format)) {
        update_texture_nv12(frame);
    } else if (is_AV_PIX_FMT_YUV42XP(frame->format)) {
        update_texture_yuv420P_yuv422P(frame);
    } else if (frame->format == AV_PIX_FMT_VDPAU) {
        update_texture_vdpau(frame);
    }
    else {
        std::cerr << "Unimplemented to texture:" << safe_av_get_pix_fmt_name((AVPixelFormat)frame->format) << std::endl;
        std::cout << all_av_hwframe_transfer_formats(frame->hw_frames_ctx);
    }
}

void GL_VideoRenderer::draw_texture_gl(const bool dev_draw_alternating_rgb_dummy_frames, int rotation_degree) {
    //GL_shaders::debug_set_swap_interval(0);
    if (egl_frame_texture.has_valid_image) {
        gl_shaders->draw_egl(egl_frame_texture.texture,rotation_degree);
    } else if (nv12_frametexture.has_valid_image) {
        gl_shaders->draw_NV12(nv12_frametexture.textures[0], nv12_frametexture.textures[1], rotation_degree);
    } else if (yuv_420_p_sw_frame_texture.has_valid_image) {
        gl_shaders->draw_YUV420P(yuv_420_p_sw_frame_texture.textures[0],
                                yuv_420_p_sw_frame_texture.textures[1],
                                yuv_420_p_sw_frame_texture.textures[2],
                                rotation_degree);
    }
    else {
        // no valid video texture yet, alternating draw the rgb textures.
        if (dev_draw_alternating_rgb_dummy_frames) {
            const auto rgb_texture = frameCount % 2==0? texture_rgb_blue:texture_rgb_green;
            gl_shaders->draw_rgb(rgb_texture, rotation_degree);
            frameCount++;
        }
    }
}

void GL_VideoRenderer::clean_video_textures_gl()
{
    if (egl_frame_texture.av_frame != nullptr) {
        av_frame_free(&egl_frame_texture.av_frame);
    }
    // For the other ones we don't keep a av frame reference around
    egl_frame_texture.has_valid_image = false;
    nv12_frametexture.has_valid_image = false;
    yuv_420_p_sw_frame_texture.has_valid_image = false;
    _current_video_width = 0;
    _current_video_height = 0;
}

void GL_VideoRenderer::mark_all_video_textures_as_without()
{
    egl_frame_texture.has_valid_image = false;
    nv12_frametexture.has_valid_image = false;
    yuv_420_p_sw_frame_texture.has_valid_image = false;
}

static std::string safe_glGetString(GLenum name){
    const auto tmp = glGetString(name);
    if (tmp == nullptr) {
        return "Unknown";
    }
    return std::string((const char*)tmp); // NOLINT(modernize-return-braced-init-list)
}

std::string GL_VideoRenderer::debug_info() {
    const auto gl_vendor = safe_glGetString(GL_VENDOR);
    const auto gl_renderer = safe_glGetString(GL_RENDERER);
    const auto gl_version = safe_glGetString(GL_VERSION);
    const auto gl_shading_language_version = safe_glGetString(GL_SHADING_LANGUAGE_VERSION);
    std::stringstream ss;
    ss<<"GL_VENDOR    : "<< gl_vendor<<"\n";
    ss<<"GL_RENDERER  : "<< gl_renderer<<"\n";
    ss<<"GL_VERSION  : "<< gl_version<<"\n";
    ss<<"GL_SHADING_LANGUAGE_VERSION : "<< gl_shading_language_version<<"\n";
    return ss.str();
}


std::vector<int> GL_VideoRenderer::supported_av_hw_formats()
{
    std::vector<int> ret;
    ret.push_back(AV_PIX_FMT_DRM_PRIME);
    return ret;
}
