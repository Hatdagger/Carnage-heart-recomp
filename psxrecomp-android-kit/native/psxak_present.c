/* Every present of the SDL renderer comes here first (the build links with
 * -Wl,--wrap=SDL_RenderPresent, see cmake/psxak.cmake).
 *
 *  - the frame-time hint (psxak_perf.c) is told when a frame is presented;
 *  - a game can draw an overlay on top of the finished frame by defining
 *    psxak_draw_overlay_sdl() (weak here). Carnage Heart draws its touch
 *    menu this way on the software renderer. On the OpenGL renderer the
 *    runtime's HUD hook (psx_host_set_hud_draw_cb) is the place for that. */
#include <SDL3/SDL.h>
#include <stdbool.h>

bool __real_SDL_RenderPresent(SDL_Renderer *renderer);
void psxak_frame_begin_present(void); /* psxak_perf.c */
void psxak_frame_end_present(void);

__attribute__((weak)) void psxak_draw_overlay_sdl(SDL_Renderer *renderer);

bool __wrap_SDL_RenderPresent(SDL_Renderer *renderer)
{
    psxak_frame_begin_present();
    if (psxak_draw_overlay_sdl) psxak_draw_overlay_sdl(renderer);
    bool ok = __real_SDL_RenderPresent(renderer);
    psxak_frame_end_present();
    return ok;
}
