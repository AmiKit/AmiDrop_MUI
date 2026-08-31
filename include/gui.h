#ifndef AMIDROP_GUI_H
#define AMIDROP_GUI_H

#include <exec/types.h>
#include <exec/lists.h>
#include "amidrop.h"
#include "server.h"
#include "qrcode.h"

struct Window;
struct Screen;
struct Gadget;
struct DrawInfo;
struct Menu;

#define GID_ABORT   2
#define GID_QUIT    3
#define GID_HISTORY 4
#define GID_CLEAR   5

#define MID_START_SERVER  1UL
#define MID_STOP_SERVER   2UL
#define MID_PREFS         3UL
#define MID_QUIT          4UL
#define MID_ABOUT         5UL

#define PREFS_ACTION_CANCEL 0
#define PREFS_ACTION_USE    1
#define PREFS_ACTION_SAVE   2

struct AmiDropGui {
    struct Screen *screen;
    APTR visual_info;
    struct Window *window;
    struct Gadget *gadget_list;
    struct Gadget *abort_gadget;
    struct Gadget *quit_gadget;
    struct Gadget *history_gadget;
    struct Gadget *clear_gadget;
    struct DrawInfo *draw_info;
    struct Menu *menu;
    ULONG signal_mask;

    WORD window_width;
    WORD window_height;
    WORD text_left;
    WORD text_right;
    WORD text_start_y;
    WORD text_step;
    WORD bar_top;
    WORD bar_bottom;
    WORD progress_right;
    WORD percent_y;
    WORD qr_left;
    WORD qr_top;
    WORD qr_width;
    WORD qr_height;
    WORD qr_area_left;
    WORD qr_area_top;
    WORD qr_area_right;
    WORD qr_area_bottom;
    WORD qr_center_x;
    WORD qr_label_y;
    BOOL show_qr;
    BOOL compact_layout;
    BOOL low_height_layout;
    BOOL show_transfer_information;

    struct List history_list;
    struct Node history_nodes[AMIDROP_TRANSFER_HISTORY];
    char history_text[AMIDROP_TRANSFER_HISTORY][AMIDROP_TRANSFER_DISPLAY_MAX];
    ULONG history_generation;

    QRCode qr;
    UBYTE qr_modules[AMIDROP_QR_BUFFER_SIZE];
    char qr_payload[96];
    BOOL qr_valid;
    BOOL qr_force_redraw;

    /* Render cache for the GadTools frontend.  Network receive activity can
       mark the server dirty for every data block, so avoid erasing/redrawing
       unchanged text and progress graphics on every iteration. */
    char rendered_lines[6][360];
    BOOL rendered_line_valid[6];
    ULONG rendered_percent;
    WORD rendered_fill_right;
    BOOL rendered_progress_valid;
    BOOL render_force;
};

BOOL gui_open(struct AmiDropGui *gui, const struct AmiDropPrefs *prefs);
void gui_close(struct AmiDropGui *gui);
void gui_redraw(struct AmiDropGui *gui, const struct AmiDropServer *server,
                const struct AmiDropPrefs *prefs);
void gui_sync_history(struct AmiDropGui *gui, const struct AmiDropServer *server);
void gui_force_qr_redraw(struct AmiDropGui *gui);
void gui_set_abort_enabled(struct AmiDropGui *gui, BOOL enabled);
ULONG gui_menu_action(struct AmiDropGui *gui, UWORD selection, UWORD *next_selection);
int gui_preferences(struct AmiDropGui *gui, struct AmiDropPrefs *prefs);
void gui_show_about(struct AmiDropGui *gui);
void gui_message(struct AmiDropGui *gui, const char *title, const char *text);
BOOL gui_confirm(struct AmiDropGui *gui, const char *title, const char *text,
                 const char *yes_text, const char *no_text);

#endif
