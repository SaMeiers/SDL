# Mali EGL framebuffer

A video driver for machines with a Mali GPU, a framebuffer, and no DRM.

## What it is for

Handhelds built on older Allwinner BSP kernels ship `/dev/fb0` and the
proprietary `mali_kbase` driver and no DRM at all: no `/dev/dri`, no
`/sys/class/drm`. The Anbernic RG35XX family on the H700 is the common
example. On those machines KMSDRM has nothing to open, X11 and Wayland have
nothing to connect to, and SDL reports "no available video device" and exits.

Mainline display support for that SoC is still out for review, so these devices
are not going to grow a DRM node on their current firmware.

The Mali EGL blob on those systems draws to the framebuffer directly. It takes
`EGL_DEFAULT_DISPLAY` as its native display and a struct of two unsigned shorts
as its native window, so the driver is mostly a matter of reading the mode out
of the framebuffer and handing EGL the right pointers. Input is the shared
Linux evdev code, the same one KMSDRM uses.

## Building

```
cmake -S . -B build -DSDL_MALI=ON
```

Off by default. A driver that cannot work is worse than no driver at all, so it
is only built where it is wanted. There is no link dependency: SDL loads
`libEGL` at run time, and the Mali headers are not present on a cross-build
host.

The driver is offered after KMSDRM. Where there is DRM that remains the better
path, and this is for machines that have none.

## Environment

`SDL_MALI_FBDEV` selects the framebuffer device. Defaults to `/dev/fb0`.

## What is not covered

Worth knowing before relying on it:

- **One device.** Tested on an Anbernic RG35XX-SP running muOS 2601, kernel
  4.9, Mali G31 with `mali_kbase`. Other members of the family should behave
  the same way, but that is expectation rather than evidence.
- **Vsync is the blob's business.** `SDL_GL_SetSwapInterval` goes to
  `eglSwapInterval` and nothing here waits on the framebuffer, so whether
  tearing appears depends on the driver. No `FBIO_WAITFORVSYNC` handling.
- **One fullscreen window.** There is a single panel and no compositor, so a
  window is always the whole screen. Position and size requests are ignored.
- **GLES only.** No Vulkan: these parts have no Vulkan driver.
- **No SDL_Surface path.** The window has to be created with `SDL_WINDOW_OPENGL`.
