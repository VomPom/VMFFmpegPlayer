#ifndef FFMPEG_PLAYER_FF_GL_RENDERER_H
#define FFMPEG_PLAYER_FF_GL_RENDERER_H

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/native_window.h>

/**
 * OpenGL ES 2.0 Renderer
 * Responsible for EGL environment initialization and OES texture rendering
 *
 * Flow: MediaCodec decode output to SurfaceTexture(OES) → Render to screen via Shader
 *
 * Note: This player directly outputs MediaCodec to ANativeWindow(Surface),
 * so this renderer is mainly for future extensions (e.g. adding simple post-processing).
 * Current playback flow renders directly via MediaCodec → Surface.
 */
class FFGLRenderer {
public:
    FFGLRenderer();
    ~FFGLRenderer();

    /**
     * Initialize EGL environment
     * @param window ANativeWindow
     * @return 0 on success
     */
    int init(ANativeWindow *window);

    /**
     * Swap buffers (present to screen)
     */
    void swapBuffers();

    /**
     * Release EGL resources
     */
    void release();

    bool isInitialized() const { return initialized; }

private:
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    EGLContext eglContext = EGL_NO_CONTEXT;
    ANativeWindow *nativeWindow = nullptr;
    bool initialized = false;
};

#endif // FFMPEG_PLAYER_FF_GL_RENDERER_H
