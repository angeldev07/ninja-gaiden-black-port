/*
 * toast.h: the achievement notice the port draws over the game's frame, like the Steam overlay.
 * The game is not patched: the GL present composites it before SwapBuffers (window only; captures
 * read the game's surface and never include it). Design: docs/target/achievements-design.md § Toast.
 *
 *   toast_show(L"ACHIEVEMENT UNLOCKED  ·  15 誉", L"Rank: Master Ninja", 0);  // any thread
 *   toast_draw(client_w, client_h, game_rect);                               // render thread, GL current
 *
 * Variant A ("seal"): a vermilion square with 誉 and a lacquer strip with the label and the name,
 * bottom centre, 8.75 % of the picture's height. Variant D (`rare`): an ensō in the seal, a second
 * vermilion rule around it, 7 s instead of 5. Toasts queue. A short drum hit, synthesized here,
 * plays when one appears (not with --mute).
 */
#ifndef NGB_TOAST_H
#define NGB_TOAST_H

#include <wchar.h>

void toast_show(const wchar_t *label, const wchar_t *name, int rare);
/* `game_rect`: left, top, right, bottom of the game picture in the window (top-left origin). */
void toast_draw(int client_w, int client_h, const int game_rect[4]);

#endif
