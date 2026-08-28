#ifndef AMIDROP_GUI_REACTION_H
#define AMIDROP_GUI_REACTION_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <graphics/gfx.h>
#include <libraries/gadtools.h>

#include "amidrop.h"
#include "qrcode.h"

struct AmiDropServer;
struct AmiDropPrefs;

#define RGUI_GID_ABORT  1
#define RGUI_GID_CLEAR  2
#define RGUI_GID_QUIT   3

#define RGUI_EVENT_NONE    0
#define RGUI_EVENT_QUIT    1
#define RGUI_EVENT_ABORT   2
#define RGUI_EVENT_CLEAR   3
#define RGUI_EVENT_MENU    4

struct AmiDropGuiEvent {
    UWORD type;
    ULONG value;
};

struct AmiDropGui {
    Object *window_obj;
    Object *root_layout;
    Object *status_obj;
    Object *address_obj;
    Object *code_obj;
    Object *receive_obj;
    Object *limit_obj;
    Object *progress_obj;
    Object *qr_obj;
    Object *history_obj;
    Object *abort_obj;
    Object *clear_obj;
    Object *quit_obj;

    struct Window *window;
    struct Screen *screen;
    APTR visual_info;
    struct Menu *menu;
    ULONG signal_mask;

    struct List history_list;
    BOOL history_list_ready;
    ULONG history_generation;

    struct BitMap *qr_bitmap;
    QRCode qr;
    uint8_t qr_modules[AMIDROP_QR_BUFFER_SIZE];
    char qr_payload[96];
    BOOL qr_valid;
    BOOL qr_force_redraw;
};

BOOL gui_open(struct AmiDropGui *gui);
void gui_close(struct AmiDropGui *gui);
void gui_redraw(struct AmiDropGui *gui, const struct AmiDropServer *server,
                const struct AmiDropPrefs *prefs);
void gui_sync_history(struct AmiDropGui *gui, const struct AmiDropServer *server);
void gui_force_qr_redraw(struct AmiDropGui *gui);
void gui_set_abort_enabled(struct AmiDropGui *gui, BOOL enabled);
ULONG gui_menu_action(struct AmiDropGui *gui, UWORD selection, UWORD *next_selection);
BOOL gui_next_event(struct AmiDropGui *gui, struct AmiDropGuiEvent *event);
int gui_preferences(struct AmiDropGui *gui, struct AmiDropPrefs *prefs);
void gui_show_about(struct AmiDropGui *gui);
void gui_message(struct AmiDropGui *gui, const char *title, const char *text);
BOOL gui_confirm(struct AmiDropGui *gui, const char *title, const char *text,
                 const char *yes_text, const char *no_text);

#endif
