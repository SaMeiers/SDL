/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* Video driver for Mali GPUs on a framebuffer, with no DRM/KMS underneath.
 *
 * Handhelds built on older Allwinner BSP kernels -- the Anbernic RG35XX family
 * on the H700, among others -- ship /dev/fb0 and the proprietary mali_kbase
 * driver and no DRM at all: there is no /dev/dri and no /sys/class/drm. The
 * KMSDRM driver cannot work there, and neither can X11 or Wayland, because
 * nothing of the sort is running.
 *
 * On those systems the Mali EGL blob draws to the framebuffer directly. It
 * takes EGL_DEFAULT_DISPLAY as its native display and a two-field struct as
 * its native window, so this driver is mostly a matter of reading the screen
 * size out of the framebuffer and handing EGL the right pointers. Input comes
 * from the shared Linux evdev code, exactly as it does for KMSDRM.
 */

#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_MALI

#include "../SDL_sysvideo.h"
#include "../../events/SDL_events_c.h"

#ifdef SDL_INPUT_LINUXEV
#include "../../core/linux/SDL_evdev.h"
#endif

#include "SDL_malivideo.h"
#include "SDL_maliopengles.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void MALI_Destroy(SDL_VideoDevice *device)
{
    SDL_free(device->internal);
    SDL_free(device);
}

static SDL_VideoDevice *MALI_Create(void)
{
    SDL_VideoDevice *device;
    SDL_VideoData *data;

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        return NULL;
    }

    data = (SDL_VideoData *)SDL_calloc(1, sizeof(SDL_VideoData));
    if (!data) {
        SDL_free(device);
        return NULL;
    }
    data->fb_fd = -1;

    device->internal = data;
    device->num_displays = 0;
    device->free = MALI_Destroy;

    device->VideoInit = MALI_VideoInit;
    device->VideoQuit = MALI_VideoQuit;
    device->CreateSDLWindow = MALI_CreateWindow;
    device->SetWindowTitle = MALI_SetWindowTitle;
    device->SetWindowPosition = MALI_SetWindowPosition;
    device->SetWindowSize = MALI_SetWindowSize;
    device->ShowWindow = MALI_ShowWindow;
    device->HideWindow = MALI_HideWindow;
    device->DestroyWindow = MALI_DestroyWindow;

#ifdef SDL_VIDEO_OPENGL_EGL
    device->GL_LoadLibrary = MALI_GLES_LoadLibrary;
    device->GL_GetProcAddress = MALI_GLES_GetProcAddress;
    device->GL_UnloadLibrary = MALI_GLES_UnloadLibrary;
    device->GL_CreateContext = MALI_GLES_CreateContext;
    device->GL_MakeCurrent = MALI_GLES_MakeCurrent;
    device->GL_SetSwapInterval = MALI_GLES_SetSwapInterval;
    device->GL_GetSwapInterval = MALI_GLES_GetSwapInterval;
    device->GL_SwapWindow = MALI_GLES_SwapWindow;
    device->GL_DestroyContext = MALI_GLES_DestroyContext;
#endif

    device->PumpEvents = MALI_PumpEvents;

    return device;
}

VideoBootStrap MALI_bootstrap = {
    "mali",
    "Mali EGL framebuffer video driver",
    MALI_Create,
    NULL, // no ShowMessageBox implementation
    false
};

/*****************************************************************************/
// Display
/*****************************************************************************/

static bool MALI_AddVideoDisplay(SDL_VideoDevice *_this)
{
    SDL_VideoData *videodata = _this->internal;
    struct fb_var_screeninfo vinfo;
    SDL_VideoDisplay display;
    SDL_DisplayMode mode;

    if (ioctl(videodata->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        return SDL_SetError("MALI: could not read the framebuffer mode");
    }

    SDL_zero(mode);
    mode.w = (int)vinfo.xres;
    mode.h = (int)vinfo.yres;

    /* The blob renders through EGL, so the framebuffer's own bit depth only
       tells us what the panel is; the surface format comes from the EGL config.
       Report the common cases and fall back to 32-bit. */
    switch (vinfo.bits_per_pixel) {
    case 16:
        mode.format = SDL_PIXELFORMAT_RGB565;
        break;
    default:
        mode.format = SDL_PIXELFORMAT_XRGB8888;
        break;
    }

    /* The framebuffer reports timings in picoseconds per pixel. When they are
       filled in they give the real refresh rate; plenty of BSP drivers leave
       them zeroed, so fall back to 60. */
    mode.refresh_rate = 60.0f;
    if (vinfo.pixclock > 0) {
        const unsigned long aTotalX = vinfo.xres + vinfo.left_margin + vinfo.right_margin + vinfo.hsync_len;
        const unsigned long aTotalY = vinfo.yres + vinfo.upper_margin + vinfo.lower_margin + vinfo.vsync_len;
        const unsigned long aPixels = aTotalX * aTotalY;
        if (aPixels > 0) {
            const double aHz = 1.0e12 / ((double)vinfo.pixclock * (double)aPixels);
            if (aHz > 20.0 && aHz < 250.0) {
                mode.refresh_rate = (float)aHz;
            }
        }
    }

    SDL_zero(display);
    display.name = "Mali framebuffer";
    display.desktop_mode = mode;
    display.internal = NULL;

    if (SDL_AddVideoDisplay(&display, false) == 0) {
        return false;
    }
    return true;
}

bool MALI_VideoInit(SDL_VideoDevice *_this)
{
    SDL_VideoData *videodata = _this->internal;
    const char *aDevice = SDL_getenv("SDL_MALI_FBDEV");

    if (!aDevice) {
        aDevice = "/dev/fb0";
    }

    videodata->fb_fd = open(aDevice, O_RDWR);
    if (videodata->fb_fd < 0) {
        return SDL_SetError("MALI: could not open %s", aDevice);
    }

    if (!MALI_AddVideoDisplay(_this)) {
        close(videodata->fb_fd);
        videodata->fb_fd = -1;
        return false;
    }

#ifdef SDL_INPUT_LINUXEV
    if (!SDL_EVDEV_Init()) {
        close(videodata->fb_fd);
        videodata->fb_fd = -1;
        return false;
    }
#endif

    return true;
}

void MALI_VideoQuit(SDL_VideoDevice *_this)
{
    SDL_VideoData *videodata = _this->internal;

#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Quit();
#endif

    if (videodata->fb_fd >= 0) {
        close(videodata->fb_fd);
        videodata->fb_fd = -1;
    }
}

/*****************************************************************************/
// Window
/*****************************************************************************/

bool MALI_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    SDL_WindowData *data;
    SDL_DisplayID aDisplayID;
    const SDL_DisplayMode *aMode;

    data = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (!data) {
        return false;
    }
    window->internal = data;

    /* There is one screen and no compositor, so a window is always the whole
       panel. Asking for anything else and getting it silently ignored would be
       worse than telling SDL the size it actually got. */
    aDisplayID = SDL_GetPrimaryDisplay();
    aMode = SDL_GetDesktopDisplayMode(aDisplayID);
    if (aMode) {
        window->x = 0;
        window->y = 0;
        window->w = aMode->w;
        window->h = aMode->h;
    }
    window->flags |= SDL_WINDOW_FULLSCREEN;

    data->native_window.width = (unsigned short)window->w;
    data->native_window.height = (unsigned short)window->h;

#ifdef SDL_VIDEO_OPENGL_EGL
    if (window->flags & SDL_WINDOW_OPENGL) {
        data->egl_surface = SDL_EGL_CreateSurface(_this, window, (NativeWindowType)&data->native_window);
        if (data->egl_surface == EGL_NO_SURFACE) {
            return SDL_SetError("MALI: could not create an EGL surface on the framebuffer");
        }
    } else {
        data->egl_surface = EGL_NO_SURFACE;
    }
#endif

    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->w, window->h);
    return true;
}

void MALI_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *data = window->internal;

    if (data) {
#ifdef SDL_VIDEO_OPENGL_EGL
        if (data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_DestroySurface(_this, data->egl_surface);
        }
#endif
        SDL_free(data);
    }
    window->internal = NULL;
}

void MALI_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
    // Nothing shows a title here.
}

bool MALI_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    // The single window covers the panel; it cannot be moved.
    return SDL_Unsupported();
}

void MALI_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    // The single window covers the panel; it cannot be resized.
}

void MALI_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);
}

void MALI_HideWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_SetMouseFocus(NULL);
    SDL_SetKeyboardFocus(NULL);
}

/*****************************************************************************/
// Events
/*****************************************************************************/

void MALI_PumpEvents(SDL_VideoDevice *_this)
{
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Poll();
#endif
}

#endif // SDL_VIDEO_DRIVER_MALI
