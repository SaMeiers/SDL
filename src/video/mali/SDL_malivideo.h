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
#include "SDL_internal.h"

#ifndef SDL_malivideo_h_
#define SDL_malivideo_h_

#include "../SDL_sysvideo.h"

#include <SDL3/SDL_egl.h>

/* The native window the Mali fbdev driver expects. It is not a handle from a
   vendor entry point, as it is on Vivante: the blob reads these two fields
   straight out of the struct the application passes to eglCreateWindowSurface,
   so the layout has to match exactly and the storage has to outlive the
   surface. */
struct mali_native_window
{
    unsigned short width;
    unsigned short height;
};

struct SDL_VideoData
{
    int fb_fd; // /dev/fb0, kept open for the lifetime of the driver
};

struct SDL_DisplayData
{
    int unused;
};

struct SDL_WindowData
{
    struct mali_native_window native_window;
    EGLSurface egl_surface;
};

// Display and window functions
bool MALI_VideoInit(SDL_VideoDevice *_this);
void MALI_VideoQuit(SDL_VideoDevice *_this);
bool MALI_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props);
void MALI_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window);
bool MALI_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window);
void MALI_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window);
void MALI_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window);
void MALI_HideWindow(SDL_VideoDevice *_this, SDL_Window *window);
void MALI_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);

// Event functions
void MALI_PumpEvents(SDL_VideoDevice *_this);

#endif // SDL_malivideo_h_
