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

#if SDL_VIDEO_DRIVER_WAYLAND

#include "../SDL_sysvideo.h"
#include "SDL_waylandvideo.h"
#include "SDL_waylandevents_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "text-input-unstable-v3-client-protocol.h"

bool Wayland_InitKeyboard(SDL_VideoDevice *_this)
{
#ifdef SDL_USE_IME
    SDL_VideoData *internal = _this->internal;
    if (!internal->text_input_manager) {
        SDL_IME_Init();
    }
#endif
    SDL_SetScancodeName(SDL_SCANCODE_APPLICATION, "Menu");

    return true;
}

void Wayland_QuitKeyboard(SDL_VideoDevice *_this)
{
#ifdef SDL_USE_IME
    SDL_VideoData *internal = _this->internal;
    if (!internal->text_input_manager) {
        SDL_IME_Quit();
    }
#endif
}

void Wayland_SeatUpdateTextInput(SDL_WaylandSeat *seat)
{
    if (seat->text_input.zwp_text_input) {
        SDL_WindowData *focus = seat->keyboard.focus;

        if (focus && focus->text_input_props.active) {
            SDL_Window *window = focus->sdlwindow;

            // Enabling will reset all state, so don't do it redundantly.
            if (!seat->text_input.enabled) {
                seat->text_input.enabled = true;
                zwp_text_input_v3_enable(seat->text_input.zwp_text_input);

                // Now that it's enabled, set the input properties
                zwp_text_input_v3_set_content_type(seat->text_input.zwp_text_input, focus->text_input_props.hint, focus->text_input_props.purpose);
                if (!SDL_RectEmpty(&window->text_input_rect)) {
                    const SDL_Rect scaled_rect = {
                        (int)SDL_floor(window->text_input_rect.x / focus->pointer_scale.x),
                        (int)SDL_floor(window->text_input_rect.y / focus->pointer_scale.y),
                        (int)SDL_ceil(window->text_input_rect.w / focus->pointer_scale.x),
                        (int)SDL_ceil(window->text_input_rect.h / focus->pointer_scale.y)
                    };
                    const int scaled_cursor = (int)SDL_floor(window->text_input_cursor / focus->pointer_scale.x);

                    SDL_copyp(&seat->text_input.text_input_rect, &scaled_rect);
                    seat->text_input.text_input_cursor = scaled_cursor;

                    // Clamp the x value so it doesn't run too far past the end of the text input area.
                    zwp_text_input_v3_set_cursor_rectangle(seat->text_input.zwp_text_input,
                                                           SDL_min(scaled_rect.x + scaled_cursor, scaled_rect.x + scaled_rect.w),
                                                           scaled_rect.y,
                                                           1,
                                                           scaled_rect.h);
                }
                zwp_text_input_v3_commit(seat->text_input.zwp_text_input);

                if (seat->keyboard.xkb.compose_state) {
                    // Reset compose state so composite and dead keys don't carry over
                    WAYLAND_xkb_compose_state_reset(seat->keyboard.xkb.compose_state);
                }
            }
        } else {
            if (seat->text_input.enabled) {
                seat->text_input.enabled = false;
                SDL_zero(seat->text_input.text_input_rect);
                seat->text_input.text_input_cursor = 0;
                zwp_text_input_v3_disable(seat->text_input.zwp_text_input);
                zwp_text_input_v3_commit(seat->text_input.zwp_text_input);
            }

            if (seat->keyboard.xkb.compose_state) {
                // Reset compose state so composite and dead keys don't carry over
                WAYLAND_xkb_compose_state_reset(seat->keyboard.xkb.compose_state);
            }
        }
    }
}

bool Wayland_StartTextInput(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    SDL_VideoData *display = _this->internal;

    if (display->text_input_manager) {
        SDL_WindowData *wind = window->internal;
        wind->text_input_props.hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE;

        switch (SDL_GetTextInputType(props)) {
        default:
        case SDL_TEXTINPUT_TYPE_TEXT:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
            break;
        case SDL_TEXTINPUT_TYPE_TEXT_NAME:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NAME;
            break;
        case SDL_TEXTINPUT_TYPE_TEXT_EMAIL:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
            break;
        case SDL_TEXTINPUT_TYPE_TEXT_USERNAME:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
            break;
        case SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
            wind->text_input_props.hint |= (ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA);
            break;
        case SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_VISIBLE:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
            break;
        case SDL_TEXTINPUT_TYPE_NUMBER:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER;
            break;
        case SDL_TEXTINPUT_TYPE_NUMBER_PASSWORD_HIDDEN:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN;
            wind->text_input_props.hint |= (ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA);
            break;
        case SDL_TEXTINPUT_TYPE_NUMBER_PASSWORD_VISIBLE:
            wind->text_input_props.purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN;
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
            break;
        }

        switch (SDL_GetTextInputCapitalization(props)) {
        default:
        case SDL_CAPITALIZE_NONE:
            break;
        case SDL_CAPITALIZE_LETTERS:
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_UPPERCASE;
            break;
        case SDL_CAPITALIZE_WORDS:
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_TITLECASE;
            break;
        case SDL_CAPITALIZE_SENTENCES:
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION;
            break;
        }

        if (SDL_GetTextInputAutocorrect(props)) {
            wind->text_input_props.hint |= (ZWP_TEXT_INPUT_V3_CONTENT_HINT_COMPLETION | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK);
        }
        if (SDL_GetTextInputMultiline(props)) {
            wind->text_input_props.hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE;
        }

        wind->text_input_props.active = true;

        SDL_WaylandSeat *seat;
        wl_list_for_each (seat, &display->seat_list, link) {
            if (seat->keyboard.focus == wind) {
                Wayland_SeatUpdateTextInput(seat);
            }
        }

        return true;
    }

    return SDL_SetError("wayland: cannot enable text input; compositor lacks support for the required zwp_text_input_v3 protocol");
}

bool Wayland_StopTextInput(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_VideoData *display = _this->internal;

    if (display->text_input_manager) {
        SDL_WaylandSeat *seat;
        SDL_WindowData *wind = window->internal;
        wind->text_input_props.active = false;

        wl_list_for_each (seat, &display->seat_list, link) {
            if (seat->keyboard.focus == wind) {
                Wayland_SeatUpdateTextInput(seat);
            }
        }
    }
#ifdef SDL_USE_IME
    else {
        SDL_IME_Reset();
    }
#endif

    return true;
}

bool Wayland_UpdateTextInputArea(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_VideoData *internal = _this->internal;
    if (internal->text_input_manager) {
        SDL_WaylandSeat *seat;
        SDL_WindowData *wind = window->internal;

        wl_list_for_each (seat, &internal->seat_list, link) {
            if (seat->text_input.zwp_text_input && seat->keyboard.focus == wind) {
                const SDL_Rect scaled_rect = {
                    (int)SDL_floor(window->text_input_rect.x / wind->pointer_scale.x),
                    (int)SDL_floor(window->text_input_rect.y / wind->pointer_scale.y),
                    (int)SDL_ceil(window->text_input_rect.w / wind->pointer_scale.x),
                    (int)SDL_ceil(window->text_input_rect.h / wind->pointer_scale.y)
                };
                const int scaled_cursor = (int)SDL_floor(window->text_input_cursor / wind->pointer_scale.x);

                if (!SDL_RectsEqual(&scaled_rect, &seat->text_input.text_input_rect) || scaled_cursor != seat->text_input.text_input_cursor) {
                    SDL_copyp(&seat->text_input.text_input_rect, &scaled_rect);
                    seat->text_input.text_input_cursor = scaled_cursor;

                    // Clamp the x value so it doesn't run too far past the end of the text input area.
                    zwp_text_input_v3_set_cursor_rectangle(seat->text_input.zwp_text_input,
                                                           SDL_min(scaled_rect.x + scaled_cursor, scaled_rect.x + scaled_rect.w),
                                                           scaled_rect.y,
                                                           1,
                                                           scaled_rect.h);
                    zwp_text_input_v3_commit(seat->text_input.zwp_text_input);
                }
            }
        }
    }
#ifdef SDL_USE_IME
    else {
        SDL_IME_UpdateTextInputArea(window);
    }
#endif
    return true;
}

bool Wayland_HasScreenKeyboardSupport(SDL_VideoDevice *_this)
{
    /* In reality, we just want to return true when the screen keyboard is the
     * _only_ way to get text input. So, in addition to checking for the text
     * input protocol, make sure we don't have any physical keyboards either.
     */
    SDL_VideoData *internal = _this->internal;
    SDL_WaylandSeat *seat;
    bool hastextmanager = (internal->text_input_manager != NULL);
    bool haskeyboard = false;

    // Check for at least one keyboard object on one seat.
    wl_list_for_each (seat, &internal->seat_list, link) {
        if (seat->keyboard.wl_keyboard) {
            haskeyboard = true;
            break;
        }
    }

    return !haskeyboard && hastextmanager;
}

#endif // SDL_VIDEO_DRIVER_WAYLAND
