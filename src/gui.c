#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "prefs.h"

extern struct Library *GadToolsBase;
extern struct Library *AslBase;
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

static void init_exec_list(struct List *list)
{
    if (!list) return;
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}

static struct NewMenu amidrop_menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Start server", (STRPTR)"S", 0, 0, (APTR)MID_START_SERVER },
    { NM_ITEM,  (STRPTR)"Stop server", (STRPTR)"T", 0, 0, (APTR)MID_STOP_SERVER },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Preferences...", (STRPTR)"P", 0, 0, (APTR)MID_PREFS },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"About AmiDrop...", NULL, 0, 0, (APTR)MID_ABOUT },
    { NM_ITEM,  (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MID_QUIT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* Requester body text is formatted by EasyRequestArgs(), but Intuition does
   not guarantee a useful automatic wrap for long single-line strings.  Keep a
   small static work buffer (AmiDrop is single-threaded) and insert explicit
   line breaks based on the active public-screen font.  Percent signs are
   doubled because EasyRequest treats the body as a RawDoFmt-style format
   string. */
static char requester_text_buffer[768];

static void append_requester_segment(char **dst, size_t *remaining,
                                     const char *src, size_t length)
{
    size_t i;
    if (!dst || !*dst || !remaining || !src) return;
    for (i = 0; i < length && *remaining > 1; ++i) {
        if (src[i] == '%' && *remaining > 2) {
            *(*dst)++ = '%';
            *(*dst)++ = '%';
            *remaining -= 2;
        } else {
            *(*dst)++ = src[i];
            --*remaining;
        }
    }
}

static const char *wrap_requester_text(struct Window *window, const char *text)
{
    struct Screen *screen = NULL;
    struct RastPort *rp = NULL;
    BOOL unlock_screen = FALSE;
    const char *p;
    char *out = requester_text_buffer;
    size_t remaining = sizeof(requester_text_buffer);
    WORD max_width = 480;

    requester_text_buffer[0] = '\0';
    if (!text || !*text) return requester_text_buffer;

    if (window && window->WScreen) {
        screen = window->WScreen;
    } else {
        screen = LockPubScreen(NULL);
        if (screen) unlock_screen = TRUE;
    }
    if (screen) {
        LONG candidate = (LONG)screen->Width - 96L;
        if (candidate > 520L) candidate = 520L;
        if (candidate < 160L) candidate = 160L;
        max_width = (WORD)candidate;
        rp = &screen->RastPort;
    }

    p = text;
    while (*p && remaining > 1) {
        const char *line_start = p;
        const char *scan = p;
        const char *last_space = NULL;
        const char *line_end = NULL;

        while (*scan && *scan != '\n') {
            size_t length = (size_t)(scan - line_start + 1);
            BOOL fits;
            if (*scan == ' ' || *scan == '\t') last_space = scan;
            if (rp)
                fits = TextLength(rp, (STRPTR)line_start, (ULONG)length) <= max_width;
            else
                fits = length <= 60;
            if (!fits) {
                if (last_space && last_space > line_start) line_end = last_space;
                else if (scan > line_start) line_end = scan;
                else line_end = scan + 1;
                break;
            }
            ++scan;
        }
        if (!line_end) line_end = scan;

        while (line_end > line_start &&
               (line_end[-1] == ' ' || line_end[-1] == '\t')) --line_end;
        append_requester_segment(&out, &remaining, line_start,
                                 (size_t)(line_end - line_start));

        p = line_end;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\n') ++p;
        if (*p && remaining > 1) {
            *out++ = '\n';
            --remaining;
        }
    }
    *out = '\0';

    if (unlock_screen) UnlockPubScreen(NULL, screen);
    return requester_text_buffer;
}

static void easy_message_window(struct Window *window, const char *title,
                                const char *text, const char *gadgets)
{
    struct EasyStruct easy;
    memset(&easy, 0, sizeof(easy));
    easy.es_StructSize = sizeof(easy);
    easy.es_Title = (STRPTR)(title ? title : "AmiDrop");
    easy.es_TextFormat = (STRPTR)wrap_requester_text(window, text);
    easy.es_GadgetFormat = (STRPTR)(gadgets ? gadgets : "OK");
    EasyRequestArgs(window, &easy, NULL, NULL);
}

static struct Gadget *create_button(struct AmiDropGui *gui, struct Gadget *previous,
                                    WORD left, WORD top, WORD width, WORD height,
                                    const char *label, UWORD gadget_id)
{
    struct NewGadget ng;
    struct TagItem tags[1];

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_Width = width;
    ng.ng_Height = height;
    ng.ng_GadgetText = (STRPTR)label;
    ng.ng_TextAttr = (gui && gui->screen) ? gui->screen->Font : NULL;
    ng.ng_GadgetID = gadget_id;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_VisualInfo = gui->visual_info;
    ng.ng_UserData = NULL;
    tags[0].ti_Tag = TAG_DONE;
    tags[0].ti_Data = 0;
    return CreateGadgetA(BUTTON_KIND, previous, &ng, tags);
}

static struct Gadget *create_history_list(struct AmiDropGui *gui, struct Gadget *previous,
                                          WORD left, WORD top, WORD width, WORD height)
{
    struct NewGadget ng;
    struct TagItem tags[4];

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = top;
    ng.ng_Width = width;
    ng.ng_Height = height;
    ng.ng_GadgetText = (STRPTR)"Successfully received (newest first)";
    ng.ng_TextAttr = (gui && gui->screen) ? gui->screen->Font : NULL;
    ng.ng_GadgetID = GID_HISTORY;
    ng.ng_Flags = PLACETEXT_ABOVE;
    ng.ng_VisualInfo = gui->visual_info;
    ng.ng_UserData = NULL;

    tags[0].ti_Tag = GTLV_Labels;
    tags[0].ti_Data = (ULONG)&gui->history_list;
    tags[1].ti_Tag = GTLV_ReadOnly;
    tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = GTLV_ScrollWidth;
    tags[2].ti_Data = 16;
    tags[3].ti_Tag = TAG_DONE;
    tags[3].ti_Data = 0;
    return CreateGadgetA(LISTVIEW_KIND, previous, &ng, tags);
}

static WORD qr_corrected_width(const struct AmiDropGui *gui, WORD qr_height)
{
    LONG corrected;

    if (!gui || !gui->screen || qr_height <= 0) return qr_height;

    /* Classic PAL/NTSC Amiga screen modes often use non-square pixels.  A
       square bitmap therefore appears vertically stretched on a 4:3 display,
       most visibly in modes such as 640x256 and 640x200.  For classic-width
       modes derive the logical QR width needed to compensate that pixel
       aspect.  Modern/widescreen modes are deliberately left alone because
       their physical display aspect cannot be inferred from the raster size. */
    if (gui->screen->Width <= 720 && gui->screen->Height <= 400) {
        corrected = ((LONG)qr_height * 3L * (LONG)gui->screen->Width +
                     2L * (LONG)gui->screen->Height) /
                    (4L * (LONG)gui->screen->Height);
        if (corrected > qr_height) return (WORD)corrected;
    }

    return qr_height;
}

static void fit_qr_dimensions(struct AmiDropGui *gui, WORD desired_height,
                              WORD max_width, WORD *qr_width, WORD *qr_height)
{
    const WORD min_size = 37; /* Version 3: 29 modules + 4-module quiet zone each side. */
    WORD width;
    WORD height;

    if (!qr_width || !qr_height) return;

    height = desired_height;
    if (height < min_size) height = min_size;
    width = qr_corrected_width(gui, height);

    if (max_width >= min_size && width > max_width) {
        LONG scaled = ((LONG)height * (LONG)max_width + (LONG)width / 2L) / (LONG)width;
        height = (WORD)scaled;
        if (height < min_size) height = min_size;
        width = qr_corrected_width(gui, height);
        if (width > max_width) width = max_width;
    }

    if (width < min_size) width = min_size;
    *qr_width = width;
    *qr_height = height;
}

static void configure_main_geometry(struct AmiDropGui *gui)
{
    WORD width;
    WORD height;
    WORD qr_max_width;
    WORD qr_anchor_right;
    WORD qr_anchor_top;

    if (!gui || !gui->screen) return;

    width = 500;
    height = 368;
    if (gui->screen->Width < (UWORD)(width + 4))
        width = gui->screen->Width > 4 ? (WORD)(gui->screen->Width - 4) : (WORD)gui->screen->Width;
    if (gui->screen->Height < (UWORD)(height + 4)) {
        /* Very low Workbench modes need more breathing room than four pixels.
           In particular a 200-line screen otherwise leaves the bottom gadgets
           inside Intuition's border/domain once GadTools applies its minimums. */
        if (gui->screen->Height <= 220)
            height = gui->screen->Height > 8 ? (WORD)(gui->screen->Height - 8) : (WORD)gui->screen->Height;
        else
            height = gui->screen->Height > 4 ? (WORD)(gui->screen->Height - 4) : (WORD)gui->screen->Height;
    }

    gui->window_width = width;
    gui->window_height = height;
    gui->compact_layout = height < 320 ? TRUE : FALSE;
    gui->low_height_layout = height <= 212 ? TRUE : FALSE;
    gui->text_left = width < 360 ? 10 : 18;

    gui->show_qr = (width >= 440) ? TRUE : FALSE;

    /* Keep the QR canvas locked to one visual anchor in every layout.
       Its top-right corner is invariant.  Aspect correction happens only
       inside that fixed canvas, where the actual QR raster is centered. */
    qr_anchor_right = (WORD)(width - 11);
    qr_anchor_top = 23;

    if (gui->low_height_layout) {
        gui->text_start_y = 30;
        gui->text_step = 13;
        gui->bar_top = 88;
        gui->bar_bottom = 103;
        gui->percent_y = 0;
    } else if (gui->compact_layout) {
        gui->text_start_y = height < 235 ? 24 : 28;
        gui->text_step = height < 235 ? 14 : 16;
        gui->bar_top = (WORD)(gui->text_start_y + gui->text_step * 6 - 6);
        gui->bar_bottom = (WORD)(gui->bar_top + 12);
        gui->percent_y = (WORD)(gui->bar_bottom + 14);
    } else {
        gui->text_start_y = 38;
        gui->text_step = 19;
        gui->bar_top = 157;
        gui->bar_bottom = 172;
        gui->percent_y = 186;
    }

    if (gui->show_qr) {
        WORD qr_area_width;
        WORD qr_area_height;

        /* The QR gets a fixed white canvas matching the space historically
           reserved for it.  Pixel-aspect correction changes only the QR
           raster inside this canvas; the canvas itself stays put.  Centering
           the corrected QR removes the visually uneven empty space that
           otherwise appears in classic 640x200/256/400 modes. */
        if (gui->low_height_layout) {
            qr_area_width = 92;
            qr_area_height = 74;
            qr_max_width = qr_area_width;
            fit_qr_dimensions(gui, qr_area_height, qr_max_width,
                              &gui->qr_width, &gui->qr_height);
            gui->qr_label_y = 0;
        } else if (!gui->compact_layout && width >= 500) {
            qr_area_width = 148;
            qr_area_height = 148;
            qr_max_width = qr_area_width;
            fit_qr_dimensions(gui, qr_area_height, qr_max_width,
                              &gui->qr_width, &gui->qr_height);
            gui->qr_label_y = (WORD)(qr_anchor_top + qr_area_height + 17);
        } else {
            qr_area_width = (WORD)(width >= 480 ? 148 : 120);
            qr_area_height = 111;
            qr_max_width = qr_area_width;
            fit_qr_dimensions(gui, qr_area_height, qr_max_width,
                              &gui->qr_width, &gui->qr_height);
            gui->qr_label_y = (WORD)(qr_anchor_top + qr_area_height + 14);
        }

        /* The white canvas is anchored by its upper-right corner.  The QR is
           then centered inside it and may grow only within that fixed area. */
        gui->qr_area_right = qr_anchor_right;
        gui->qr_area_top = qr_anchor_top;
        gui->qr_area_left = (WORD)(gui->qr_area_right - qr_area_width + 1);
        gui->qr_area_bottom = (WORD)(gui->qr_area_top + qr_area_height - 1);
        gui->qr_left = (WORD)(gui->qr_area_left +
                              (qr_area_width - gui->qr_width) / 2);
        gui->qr_top = (WORD)(gui->qr_area_top +
                             (qr_area_height - gui->qr_height) / 2);
        gui->qr_center_x = (WORD)((gui->qr_area_left + gui->qr_area_right) / 2);
        gui->text_right = (WORD)(gui->qr_area_left - 8);
    } else {
        gui->qr_width = 0;
        gui->qr_height = 0;
        gui->qr_left = gui->qr_top = 0;
        gui->qr_area_left = gui->qr_area_top = 0;
        gui->qr_area_right = gui->qr_area_bottom = 0;
        gui->qr_center_x = gui->qr_label_y = 0;
        gui->text_right = (WORD)(width - gui->text_left);
    }

    if (gui->low_height_layout && gui->show_qr)
        gui->progress_right = (WORD)(gui->text_right - 34);
    else
        gui->progress_right = gui->text_right;
}

BOOL gui_open(struct AmiDropGui *gui)
{
    struct Gadget *context;
    struct Gadget *last;
    struct TagItem vi_tags[1];
    struct TagItem menu_tags[2];
    struct TagItem win_tags[12];
    LONG left;
    LONG top;
    WORD history_top;
    WORD history_height;
    WORD buttons_top;
    WORD button_width;
    WORD gap;
    WORD buttons_left;
    int ti = 0;

    if (!gui) return FALSE;
    memset(gui, 0, sizeof(*gui));
    init_exec_list(&gui->history_list);
    gui->history_generation = ~0UL;
    gui->qr_force_redraw = TRUE;
    gui->render_force = TRUE;

    gui->screen = LockPubScreen(NULL);
    if (!gui->screen) return FALSE;
    configure_main_geometry(gui);

    vi_tags[0].ti_Tag = TAG_DONE;
    vi_tags[0].ti_Data = 0;
    gui->visual_info = GetVisualInfoA(gui->screen, vi_tags);
    if (!gui->visual_info) {
        UnlockPubScreen(NULL, gui->screen);
        gui->screen = NULL;
        return FALSE;
    }

    gui->menu = CreateMenusA(amidrop_menus, NULL);
    menu_tags[0].ti_Tag = GTMN_NewLookMenus;
    menu_tags[0].ti_Data = TRUE;
    menu_tags[1].ti_Tag = TAG_DONE;
    menu_tags[1].ti_Data = 0;
    if (!gui->menu || !LayoutMenusA(gui->menu, gui->visual_info, menu_tags)) {
        gui_close(gui);
        return FALSE;
    }

    context = CreateContext(&gui->gadget_list);
    if (!context) {
        gui_close(gui);
        return FALSE;
    }
    last = context;

    if (gui->low_height_layout) {
        /* Keep a real, usable GadTools ListView.  The previous 8-pixel fallback
           was below the practical ListView domain on a 200-line Workbench and
           could make OpenWindowTagList() fail. */
        buttons_top = (WORD)(gui->window_height - 30);
        /* Percentage is inside the progress bar, so the ListView heading can
           start comfortably below both the bar and the compact QR block. */
        history_top = 118;
        history_height = (WORD)(buttons_top - history_top - 10);
        if (history_height < 24) {
            history_height = 24;
            history_top = (WORD)(buttons_top - history_height - 10);
        }
    } else {
        buttons_top = (WORD)(gui->window_height - (gui->compact_layout ? 31 : 39));
        if (buttons_top < 120) buttons_top = 120;
        if (buttons_top > gui->window_height - 26) buttons_top = (WORD)(gui->window_height - 26);
        history_top = gui->compact_layout ? (WORD)(gui->percent_y + 20) : 212;
        history_height = (WORD)(buttons_top - history_top - 16);
        if (history_height < 24) {
            history_height = 24;
            history_top = (WORD)(buttons_top - history_height - 12);
        }
    }

    gui->history_gadget = create_history_list(gui, last, gui->text_left, history_top,
        (WORD)(gui->window_width - gui->text_left * 2), history_height);
    if (!gui->history_gadget) { gui_close(gui); return FALSE; }
    last = gui->history_gadget;

    if (gui->window_width >= 500) {
        buttons_left = 72;
        button_width = 112;
        gap = 10;
    } else {
        gap = gui->window_width < 360 ? 5 : 10;
        buttons_left = gui->text_left;
        button_width = (WORD)((gui->window_width - buttons_left * 2 - gap * 2) / 3);
    }

    gui->abort_gadget = create_button(gui, last, buttons_left, buttons_top, button_width, 22,
                                      "Abort transfer", GID_ABORT);
    if (!gui->abort_gadget) { gui_close(gui); return FALSE; }
    last = gui->abort_gadget;

    gui->clear_gadget = create_button(gui, last, (WORD)(buttons_left + button_width + gap),
                                      buttons_top, button_width, 22, "Clear list", GID_CLEAR);
    if (!gui->clear_gadget) { gui_close(gui); return FALSE; }
    last = gui->clear_gadget;

    gui->quit_gadget = create_button(gui, last, (WORD)(buttons_left + (button_width + gap) * 2),
                                     buttons_top, button_width, 22, "Quit", GID_QUIT);
    if (!gui->quit_gadget) { gui_close(gui); return FALSE; }

    left = ((LONG)gui->screen->Width - gui->window_width) / 2;
    top = ((LONG)gui->screen->Height - gui->window_height) / 2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;

    win_tags[ti].ti_Tag = WA_Left; win_tags[ti++].ti_Data = (ULONG)left;
    win_tags[ti].ti_Tag = WA_Top; win_tags[ti++].ti_Data = (ULONG)top;
    win_tags[ti].ti_Tag = WA_Width; win_tags[ti++].ti_Data = gui->window_width;
    win_tags[ti].ti_Tag = WA_Height; win_tags[ti++].ti_Data = gui->window_height;
    win_tags[ti].ti_Tag = WA_IDCMP; win_tags[ti++].ti_Data = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK;
    win_tags[ti].ti_Tag = WA_Flags; win_tags[ti++].ti_Data = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIMPLE_REFRESH;
    win_tags[ti].ti_Tag = WA_Gadgets; win_tags[ti++].ti_Data = (ULONG)gui->gadget_list;
    win_tags[ti].ti_Tag = WA_Title; win_tags[ti++].ti_Data = (ULONG)"AmiDrop " AMIDROP_VERSION " - file receiver";
    win_tags[ti].ti_Tag = WA_PubScreen; win_tags[ti++].ti_Data = (ULONG)gui->screen;
    win_tags[ti].ti_Tag = WA_NewLookMenus; win_tags[ti++].ti_Data = TRUE;
    win_tags[ti].ti_Tag = WA_AutoAdjust; win_tags[ti++].ti_Data = TRUE;
    win_tags[ti].ti_Tag = TAG_DONE; win_tags[ti].ti_Data = 0;

    gui->window = OpenWindowTagList(NULL, win_tags);
    if (!gui->window) {
        gui_close(gui);
        return FALSE;
    }

    if (gui->menu) SetMenuStrip(gui->window, gui->menu);
    gui->draw_info = GetScreenDrawInfo(gui->screen);
    gui->signal_mask = 1UL << gui->window->UserPort->mp_SigBit;
    GT_RefreshWindow(gui->window, NULL);
    gui_set_abort_enabled(gui, FALSE);
    return TRUE;
}

void gui_close(struct AmiDropGui *gui)
{
    if (!gui) return;
    if (gui->draw_info && gui->screen) {
        FreeScreenDrawInfo(gui->screen, gui->draw_info);
        gui->draw_info = NULL;
    }
    if (gui->window) {
        if (gui->menu) ClearMenuStrip(gui->window);
        CloseWindow(gui->window);
        gui->window = NULL;
    }
    if (gui->menu) {
        FreeMenus(gui->menu);
        gui->menu = NULL;
    }
    if (gui->gadget_list) {
        FreeGadgets(gui->gadget_list);
        gui->gadget_list = NULL;
    }
    if (gui->visual_info) {
        FreeVisualInfo(gui->visual_info);
        gui->visual_info = NULL;
    }
    if (gui->screen) {
        UnlockPubScreen(NULL, gui->screen);
        gui->screen = NULL;
    }
}

static void draw_text(struct RastPort *rp, WORD x, WORD y, const char *text)
{
    if (!rp || !text) return;
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (ULONG)strlen(text));
}

static void draw_text_centered(struct RastPort *rp, WORD center_x, WORD y, const char *text)
{
    WORD width;
    WORD x;

    if (!rp || !text) return;
    width = (WORD)TextLength(rp, (STRPTR)text, (ULONG)strlen(text));
    x = (WORD)(center_x - width / 2);
    draw_text(rp, x, y, text);
}

static void draw_text_fitted(struct RastPort *rp, WORD x, WORD y,
                             const char *text, WORD right_edge)
{
    char fitted[360];
    size_t text_len;
    size_t keep;
    WORD max_width;

    if (!rp || !text || right_edge < x) return;
    max_width = (WORD)(right_edge - x + 1);

    if (TextLength(rp, (STRPTR)text, (ULONG)strlen(text)) <= max_width) {
        draw_text(rp, x, y, text);
        return;
    }

    text_len = strlen(text);
    keep = text_len;
    if (keep > sizeof(fitted) - 4) keep = sizeof(fitted) - 4;

    while (keep > 0) {
        memcpy(fitted, text, keep);
        fitted[keep] = '.';
        fitted[keep + 1] = '.';
        fitted[keep + 2] = '.';
        fitted[keep + 3] = '\0';
        if (TextLength(rp, (STRPTR)fitted, (ULONG)(keep + 3)) <= max_width) {
            draw_text(rp, x, y, fitted);
            return;
        }
        --keep;
    }

    if (TextLength(rp, (STRPTR)"...", 3) <= max_width)
        draw_text(rp, x, y, "...");
}

static ULONG pen_brightness(struct AmiDropGui *gui, UWORD pen)
{
    ULONG rgb;
    if (!gui || !gui->screen || !gui->screen->ViewPort.ColorMap) return 0;
    rgb = GetRGB4(gui->screen->ViewPort.ColorMap, pen);
    return ((rgb >> 8) & 15UL) + ((rgb >> 4) & 15UL) + (rgb & 15UL);
}

static void qr_pens(struct AmiDropGui *gui, UWORD *dark, UWORD *light)
{
    UWORD candidates[5];
    ULONG darkest = ~0UL;
    ULONG lightest = 0;
    int i;

    if (!dark || !light) return;
    *dark = 1;
    *light = 0;
    if (!gui || !gui->draw_info) return;

    candidates[0] = gui->draw_info->dri_Pens[TEXTPEN];
    candidates[1] = gui->draw_info->dri_Pens[BACKGROUNDPEN];
    candidates[2] = gui->draw_info->dri_Pens[SHINEPEN];
    candidates[3] = gui->draw_info->dri_Pens[SHADOWPEN];
    candidates[4] = gui->draw_info->dri_Pens[FILLPEN];

    for (i = 0; i < 5; ++i) {
        ULONG b = pen_brightness(gui, candidates[i]);
        if (b < darkest) { darkest = b; *dark = candidates[i]; }
        if (b > lightest) { lightest = b; *light = candidates[i]; }
    }
}

static BOOL update_qr(struct AmiDropGui *gui, const struct AmiDropServer *server)
{
    char payload[sizeof(gui->qr_payload)];
    BOOL was_valid;

    if (!gui) return FALSE;
    was_valid = gui->qr_valid;

    if (!server || !server->address[0] || !server->session_token[0]) {
        gui->qr_valid = FALSE;
        gui->qr_payload[0] = '\0';
        return was_valid;
    }

    snprintf(payload, sizeof(payload), "%s?t=%s", server->address, server->session_token);
    if (strlen(payload) > AMIDROP_QR_MAX_PAYLOAD) {
        gui->qr_valid = FALSE;
        gui->qr_payload[0] = '\0';
        return was_valid;
    }

    if (gui->qr_valid && strcmp(gui->qr_payload, payload) == 0) return FALSE;

    memset(gui->qr_modules, 0, sizeof(gui->qr_modules));
    if (qrcode_initText(&gui->qr, gui->qr_modules, AMIDROP_QR_VERSION,
                        ECC_LOW, payload) == 0) {
        strncpy(gui->qr_payload, payload, sizeof(gui->qr_payload) - 1);
        gui->qr_payload[sizeof(gui->qr_payload) - 1] = '\0';
        gui->qr_valid = TRUE;
    } else {
        gui->qr_payload[0] = '\0';
        gui->qr_valid = FALSE;
    }
    return TRUE;
}

static void draw_qr(struct AmiDropGui *gui, struct RastPort *rp)
{
    const WORD quiet = 4;
    WORD cells;
    UWORD dark;
    UWORD light;
    uint8_t y;

    if (!gui || !rp || !gui->qr_valid || !gui->show_qr ||
        gui->qr_width <= 0 || gui->qr_height <= 0) return;

    cells = (WORD)(gui->qr.size + quiet * 2);
    qr_pens(gui, &dark, &light);

    SetAPen(rp, light);
    RectFill(rp, gui->qr_left, gui->qr_top,
             (WORD)(gui->qr_left + gui->qr_width - 1),
             (WORD)(gui->qr_top + gui->qr_height - 1));

    SetAPen(rp, dark);
    for (y = 0; y < gui->qr.size; ++y) {
        uint8_t x;
        for (x = 0; x < gui->qr.size; ++x) {
            if (qrcode_getModule(&gui->qr, x, y)) {
                WORD x1 = (WORD)(gui->qr_left +
                    ((LONG)(x + quiet) * gui->qr_width) / cells);
                WORD x2 = (WORD)(gui->qr_left +
                    ((LONG)(x + quiet + 1) * gui->qr_width) / cells - 1);
                WORD y1 = (WORD)(gui->qr_top +
                    ((LONG)(y + quiet) * gui->qr_height) / cells);
                WORD y2 = (WORD)(gui->qr_top +
                    ((LONG)(y + quiet + 1) * gui->qr_height) / cells - 1);
                if (x2 >= x1 && y2 >= y1)
                    RectFill(rp, x1, y1, x2, y2);
            }
        }
    }
}

static void redraw_qr_area(struct AmiDropGui *gui, struct RastPort *rp,
                           UWORD text_pen, UWORD back_pen)
{
    UWORD dark_pen;
    UWORD light_pen;
    WORD clear_bottom;

    if (!gui || !rp) return;

    if (!gui->show_qr) {
        gui->qr_force_redraw = FALSE;
        return;
    }

    /* Clear the complete QR/label region only when the QR really changed or
       Intuition asks for a refresh.  The fixed QR canvas itself is then filled
       with the lightest available screen pen so it reads as a white card even
       when the aspect-corrected QR raster is smaller than that canvas. */
    clear_bottom = gui->qr_area_bottom;
    if (gui->qr_label_y > clear_bottom) clear_bottom = (WORD)(gui->qr_label_y + 3);
    SetAPen(rp, back_pen);
    RectFill(rp, gui->qr_area_left, gui->qr_area_top,
             gui->qr_area_right, clear_bottom);

    qr_pens(gui, &dark_pen, &light_pen);
    SetAPen(rp, light_pen);
    RectFill(rp, gui->qr_area_left, gui->qr_area_top,
             gui->qr_area_right, gui->qr_area_bottom);

    draw_qr(gui, rp);
    SetAPen(rp, text_pen);
    SetDrMd(rp, JAM1);
    if (gui->qr_valid && gui->qr_label_y > 0)
        draw_text_centered(rp, gui->qr_center_x, gui->qr_label_y, "Scan with phone");

    gui->qr_force_redraw = FALSE;
}

void gui_force_qr_redraw(struct AmiDropGui *gui)
{
    if (gui) {
        gui->qr_force_redraw = TRUE;
        /* This function is also used for IDCMP_REFRESHWINDOW.  Force the
           cached text/progress graphics to be reconstructed after Intuition
           has exposed or refreshed the window. */
        gui->render_force = TRUE;
    }
}

void gui_sync_history(struct AmiDropGui *gui, const struct AmiDropServer *server)
{
    struct TagItem tags[2];
    UWORD i;

    if (!gui || !gui->window || !gui->history_gadget || !server) return;
    if (gui->history_generation == server->transfer_generation) return;

    /* GadTools requires the List to be detached before its nodes are changed. */
    tags[0].ti_Tag = GTLV_Labels;
    tags[0].ti_Data = ~0UL;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    GT_SetGadgetAttrsA(gui->history_gadget, gui->window, NULL, tags);

    init_exec_list(&gui->history_list);
    memset(gui->history_nodes, 0, sizeof(gui->history_nodes));
    memset(gui->history_text, 0, sizeof(gui->history_text));

    for (i = 0; i < server->transfer_count && i < AMIDROP_TRANSFER_HISTORY; ++i) {
        strncpy(gui->history_text[i], server->transfers[i].display,
                sizeof(gui->history_text[i]) - 1);
        gui->history_text[i][sizeof(gui->history_text[i]) - 1] = '\0';
        gui->history_nodes[i].ln_Name = gui->history_text[i];
        AddTail(&gui->history_list, &gui->history_nodes[i]);
    }

    tags[0].ti_Tag = GTLV_Labels;
    tags[0].ti_Data = (ULONG)&gui->history_list;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    GT_SetGadgetAttrsA(gui->history_gadget, gui->window, NULL, tags);
    gui->history_generation = server->transfer_generation;
}

static void clear_text_line(struct AmiDropGui *gui, struct RastPort *rp,
                            WORD baseline, UWORD back_pen)
{
    WORD font_height = 8;
    WORD top;
    WORD bottom;

    if (!gui || !rp) return;
    if (rp->Font && rp->Font->tf_YSize > 0) font_height = (WORD)rp->Font->tf_YSize;
    top = (WORD)(baseline - font_height + 1);
    bottom = (WORD)(baseline + 2);
    if (top < gui->window->BorderTop) top = gui->window->BorderTop;

    SetAPen(rp, back_pen);
    RectFill(rp, (WORD)(gui->text_left > 3 ? gui->text_left - 3 : 0),
             top, gui->text_right, bottom);
}

static void draw_cached_line(struct AmiDropGui *gui, struct RastPort *rp,
                             UWORD slot, WORD baseline, const char *text,
                             UWORD text_pen, UWORD back_pen)
{
    if (!gui || !rp || !text || slot >= 6) return;
    if (!gui->render_force && gui->rendered_line_valid[slot] &&
        strcmp(gui->rendered_lines[slot], text) == 0) return;

    clear_text_line(gui, rp, baseline, back_pen);
    SetAPen(rp, text_pen);
    SetDrMd(rp, JAM1);
    draw_text_fitted(rp, gui->text_left, baseline, text, gui->text_right);

    strncpy(gui->rendered_lines[slot], text, sizeof(gui->rendered_lines[slot]) - 1);
    gui->rendered_lines[slot][sizeof(gui->rendered_lines[slot]) - 1] = '\0';
    gui->rendered_line_valid[slot] = TRUE;
}

static WORD progress_fill_right(const struct AmiDropGui *gui, ULONG percent)
{
    WORD inner_width;
    if (!gui) return 0;
    inner_width = (WORD)(gui->progress_right - gui->text_left - 3);
    if (inner_width < 0) inner_width = 0;
    return (WORD)(gui->text_left + 2 + (WORD)((inner_width * percent) / 100UL));
}

static void draw_progress_cached(struct AmiDropGui *gui, struct RastPort *rp,
                                 ULONG percent, UWORD text_pen, UWORD back_pen,
                                 UWORD fill_pen)
{
    WORD bar_left;
    WORD bar_right;
    WORD new_fill_right;
    char line[16];

    if (!gui || !rp) return;
    bar_left = gui->text_left;
    bar_right = gui->progress_right;
    new_fill_right = progress_fill_right(gui, percent);
    snprintf(line, sizeof(line), "%lu%%", (unsigned long)percent);

    if (gui->render_force || !gui->rendered_progress_valid) {
        SetAPen(rp, back_pen);
        RectFill(rp, bar_left, gui->bar_top, bar_right, gui->bar_bottom);
        SetAPen(rp, text_pen);
        Move(rp, bar_left, gui->bar_top); Draw(rp, bar_right, gui->bar_top);
        Draw(rp, bar_right, gui->bar_bottom); Draw(rp, bar_left, gui->bar_bottom);
        Draw(rp, bar_left, gui->bar_top);
        if (new_fill_right > bar_left + 1) {
            SetAPen(rp, fill_pen);
            RectFill(rp, bar_left + 2, gui->bar_top + 2,
                     new_fill_right, gui->bar_bottom - 2);
        }
    } else if (percent != gui->rendered_percent) {
        if (gui->low_height_layout) {
            WORD font_height = (rp->Font && rp->Font->tf_YSize > 0) ? (WORD)rp->Font->tf_YSize : 8;
            WORD bar_height = (WORD)(gui->bar_bottom - gui->bar_top + 1);
            WORD baseline = (WORD)(gui->bar_top + (bar_height - font_height) / 2 + font_height - 1);
            char old_line[16];
            snprintf(old_line, sizeof(old_line), "%lu%%", (unsigned long)gui->rendered_percent);
            /* COMPLEMENT is self-erasing: draw the old label a second time
               before changing the fill underneath it. */
            SetDrMd(rp, COMPLEMENT);
            draw_text_centered(rp, (WORD)((bar_left + bar_right) / 2), baseline, old_line);
            SetDrMd(rp, JAM1);
        }

        if (new_fill_right > gui->rendered_fill_right) {
            WORD left = (WORD)(gui->rendered_fill_right + 1);
            if (left < bar_left + 2) left = (WORD)(bar_left + 2);
            SetAPen(rp, fill_pen);
            RectFill(rp, left, gui->bar_top + 2, new_fill_right, gui->bar_bottom - 2);
        } else if (new_fill_right < gui->rendered_fill_right) {
            WORD left = (WORD)(new_fill_right + 1);
            if (left < bar_left + 2) left = (WORD)(bar_left + 2);
            SetAPen(rp, back_pen);
            RectFill(rp, left, gui->bar_top + 2,
                     gui->rendered_fill_right, gui->bar_bottom - 2);
        }
    } else {
        return;
    }

    SetAPen(rp, text_pen);
    if (gui->low_height_layout) {
        WORD font_height = (rp->Font && rp->Font->tf_YSize > 0) ? (WORD)rp->Font->tf_YSize : 8;
        WORD bar_height = (WORD)(gui->bar_bottom - gui->bar_top + 1);
        WORD baseline = (WORD)(gui->bar_top + (bar_height - font_height) / 2 + font_height - 1);
        SetDrMd(rp, COMPLEMENT);
        draw_text_centered(rp, (WORD)((bar_left + bar_right) / 2), baseline, line);
        SetDrMd(rp, JAM1);
    } else {
        clear_text_line(gui, rp, gui->percent_y, back_pen);
        SetAPen(rp, text_pen);
        SetDrMd(rp, JAM1);
        draw_text_centered(rp, (WORD)((bar_left + bar_right) / 2), gui->percent_y, line);
    }

    gui->rendered_percent = percent;
    gui->rendered_fill_right = new_fill_right;
    gui->rendered_progress_valid = TRUE;
}

static void format_limit_text(char *dst, size_t dst_size, ULONG max_file_kb, const char *prefix)
{
    if (!dst || dst_size == 0) return;
    if (!prefix) prefix = "";
    if (max_file_kb < 1024UL) {
        snprintf(dst, dst_size, "%s%lu KB/file", prefix, (unsigned long)max_file_kb);
    } else if (max_file_kb < 1048576UL) {
        snprintf(dst, dst_size, "%s%lu MB/file", prefix, (unsigned long)(max_file_kb / 1024UL));
    } else {
        snprintf(dst, dst_size, "%s1 GB/file", prefix);
    }
}

/* Split the status value at a word boundary using the actual screen font.
   Two rows are enough for all status strings emitted by server.c, including
   the longest authentication message, on the compact 640x256 layout. */
static void format_status_lines(struct RastPort *rp, WORD left, WORD right,
                                const char *status,
                                char *first, size_t first_size,
                                char *second, size_t second_size)
{
    static const char prefix[] = "Status:   ";
    static const char indent[] = "          ";
    WORD total_width;
    WORD prefix_width;
    WORD value_width;
    size_t len;
    size_t fit;
    size_t split;
    const char *rest;

    if (!first || first_size == 0 || !second || second_size == 0) return;
    if (!status) status = "";
    first[0] = '\0';
    second[0] = '\0';
    if (!rp || right < left) {
        snprintf(first, first_size, "%s%s", prefix, status);
        return;
    }

    total_width = (WORD)(right - left + 1);
    prefix_width = (WORD)TextLength(rp, (STRPTR)prefix,
                                    (ULONG)(sizeof(prefix) - 1));
    value_width = (WORD)(total_width - prefix_width);
    if (value_width < 8) value_width = 8;
    len = strlen(status);

    if (TextLength(rp, (STRPTR)status, (ULONG)len) <= value_width) {
        snprintf(first, first_size, "%s%s", prefix, status);
        return;
    }

    fit = 0;
    while (fit < len &&
           TextLength(rp, (STRPTR)status, (ULONG)(fit + 1)) <= value_width)
        ++fit;

    split = fit;
    while (split > 0 && status[split] != ' ' && status[split] != '\t') --split;
    if (split == 0) split = fit;
    if (split == 0 && len > 0) split = 1;

    snprintf(first, first_size, "%s%.*s", prefix, (int)split, status);
    rest = status + split;
    while (*rest == ' ' || *rest == '\t') ++rest;
    if (*rest) snprintf(second, second_size, "%s%s", indent, rest);
}

void gui_redraw(struct AmiDropGui *gui, const struct AmiDropServer *server,
                const struct AmiDropPrefs *prefs)
{
    struct RastPort *rp;
    UWORD text_pen = 1;
    UWORD back_pen = 0;
    UWORD fill_pen = 1;
    char line[360];
    ULONG percent = 0;
    WORD y;
    UWORD slot = 0;

    if (!gui || !gui->window || !server || !prefs) return;
    rp = gui->window->RPort;
    if (gui->draw_info) {
        text_pen = gui->draw_info->dri_Pens[TEXTPEN];
        back_pen = gui->draw_info->dri_Pens[BACKGROUNDPEN];
        fill_pen = gui->draw_info->dri_Pens[FILLPEN];
    }

    if (update_qr(gui, server)) gui->qr_force_redraw = TRUE;

    /* Do not wipe the entire status area here.  During a transfer the server
       becomes dirty for every received network block; erasing the whole area
       on each block caused visible flashing on classic Amiga displays.  Each
       line below now redraws only when its contents actually change. */
    y = gui->text_start_y;

    /* The version already lives in the window title.  Reuse that row for a
       second status line so compact screens never have to throw away useful
       status/error information just to repeat the version string. */
    snprintf(line, sizeof(line), "Address:  %s",
             server->address[0] ? server->address : "Server not running");
    draw_cached_line(gui, rp, slot++, y, line, text_pen, back_pen);
    y = (WORD)(y + gui->text_step);

    snprintf(line, sizeof(line), "Code:     %s   (PC)",
             server->access_code[0] ? server->access_code : "------");
    draw_cached_line(gui, rp, slot++, y, line, text_pen, back_pen);
    y = (WORD)(y + gui->text_step);

    snprintf(line, sizeof(line), "Receive:  %s", prefs->receive_dir);
    draw_cached_line(gui, rp, slot++, y, line, text_pen, back_pen);
    y = (WORD)(y + gui->text_step);

    {
        char continuation[160];
        format_status_lines(rp, gui->text_left, gui->text_right, server->status,
                            line, sizeof(line), continuation, sizeof(continuation));
        draw_cached_line(gui, rp, slot++, y, line, text_pen, back_pen);
        y = (WORD)(y + gui->text_step);
        draw_cached_line(gui, rp, slot++, y, continuation, text_pen, back_pen);
    }

    if (!gui->low_height_layout) {
        y = (WORD)(y + gui->text_step);
        format_limit_text(line, sizeof(line), prefs->max_file_kb, "Limit:    ");
        draw_cached_line(gui, rp, slot++, y, line, text_pen, back_pen);
    }

    if (gui->qr_force_redraw) redraw_qr_area(gui, rp, text_pen, back_pen);

    if (server->uploading && server->upload_total > 0) {
        percent = (ULONG)(((unsigned long long)server->upload_received * 100ULL) / server->upload_total);
        if (percent > 100) percent = 100;
    } else if (strncmp(server->status, "Received:", 9) == 0) {
        percent = 100;
    }
    draw_progress_cached(gui, rp, percent, text_pen, back_pen, fill_pen);

    gui->render_force = FALSE;
}

void gui_set_abort_enabled(struct AmiDropGui *gui, BOOL enabled)
{
    struct TagItem tags[2];
    if (!gui || !gui->window || !gui->abort_gadget) return;
    tags[0].ti_Tag = GA_Disabled;
    tags[0].ti_Data = enabled ? FALSE : TRUE;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    GT_SetGadgetAttrsA(gui->abort_gadget, gui->window, NULL, tags);
}

ULONG gui_menu_action(struct AmiDropGui *gui, UWORD selection, UWORD *next_selection)
{
    struct MenuItem *item;
    if (next_selection) *next_selection = MENUNULL;
    if (!gui || !gui->menu || selection == MENUNULL) return 0;
    item = ItemAddress(gui->menu, selection);
    if (!item) return 0;
    if (next_selection) *next_selection = item->NextSelect;
    return (ULONG)GTMENUITEM_USERDATA(item);
}

void gui_message(struct AmiDropGui *gui, const char *title, const char *text)
{
    easy_message_window(gui ? gui->window : NULL, title, text, "OK");
}

BOOL gui_confirm(struct AmiDropGui *gui, const char *title, const char *text,
                 const char *yes_text, const char *no_text)
{
    struct EasyStruct easy;
    char gadgets[96];

    snprintf(gadgets, sizeof(gadgets), "%s|%s", yes_text ? yes_text : "Yes",
             no_text ? no_text : "No");
    memset(&easy, 0, sizeof(easy));
    easy.es_StructSize = sizeof(easy);
    easy.es_Title = (STRPTR)(title ? title : "AmiDrop");
    easy.es_TextFormat = (STRPTR)wrap_requester_text(gui ? gui->window : NULL, text);
    easy.es_GadgetFormat = (STRPTR)gadgets;
    return EasyRequestArgs(gui ? gui->window : NULL, &easy, NULL, NULL) == 1;
}

static void draw_about_contents(struct AmiDropGui *gui, struct Window *window)
{
    struct RastPort *rp;
    UWORD text_pen = 1;
    WORD center_x;
    WORD y;
    WORD line_height;

    if (!gui || !window) return;
    rp = window->RPort;
    if (!rp) return;

    if (gui->draw_info) text_pen = gui->draw_info->dri_Pens[TEXTPEN];
    SetAPen(rp, text_pen);
    SetDrMd(rp, JAM1);

    center_x = (WORD)(window->Width / 2);
    line_height = (gui->screen && gui->screen->Font)
                    ? (WORD)(gui->screen->Font->ta_YSize + 4) : 12;
    y = (WORD)(window->BorderTop + line_height + 7);

    draw_text_centered(rp, center_x, y,
                       "AmiDrop " AMIDROP_VERSION " (" AMIDROP_DATE ")");
    y += line_height;
    draw_text_centered(rp, center_x, y, "File transfer for AmigaOS");
    y += (WORD)(line_height + 4);
    draw_text_centered(rp, center_x, y,
                       "(c) 2026 Andreas 'Andiweli' St\374rmer");
    y += (WORD)(line_height + 4);
    draw_text_centered(rp, center_x, y, "Icon by Mason");
    y += line_height;
    draw_text_centered(rp, center_x, y, "QR code generator:");
    y += line_height;
    draw_text_centered(rp, center_x, y, "Richard Moore / Project Nayuki");
    y += line_height;
    draw_text_centered(rp, center_x, y,
                       "MIT licensed");
}


static WORD about_window_width(struct AmiDropGui *gui)
{
    static const char *lines[] = {
        "AmiDrop " AMIDROP_VERSION " (" AMIDROP_DATE ")",
        "File transfer for AmigaOS",
        "(c) 2026 Andreas 'Andiweli' St\374rmer",
        "Icon by Mason",
        "QR code generator:",
        "Richard Moore / Project Nayuki",
        "MIT licensed",
        NULL
    };
    struct RastPort *rp;
    WORD widest = 0;
    WORD width;
    WORD max_width;
    int i;

    if (!gui || !gui->screen) return 320;
    rp = &gui->screen->RastPort;

    for (i = 0; lines[i] != NULL; ++i) {
        WORD line_width = (WORD)TextLength(rp, (STRPTR)lines[i],
                                           (ULONG)strlen(lines[i]));
        if (line_width > widest) widest = line_width;
    }

    /* 16 pixels of breathing room on each side, in addition to the
       normal Intuition window borders. */
    width = (WORD)(widest + 32);
    if (width < 240) width = 240;

    max_width = (WORD)(gui->screen->Width - 16);
    if (max_width >= 200 && width > max_width) width = max_width;
    return width;
}

void gui_show_about(struct AmiDropGui *gui)
{
    struct Window *window = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *context;
    struct Gadget *ok_gadget;
    struct IntuiMessage *message;
    LONG left;
    LONG top;
    BOOL done = FALSE;
    WORD width;
    WORD height;
    WORD ok_left;
    WORD ok_top;
    WORD line_height;

    if (!gui || !gui->screen || !gui->visual_info) return;

    width = about_window_width(gui);
    line_height = (gui->screen && gui->screen->Font)
                    ? (WORD)(gui->screen->Font->ta_YSize + 4) : 12;
    height = (WORD)(158 + line_height);
    ok_left = (WORD)((width - 88) / 2);
    ok_top = (WORD)(119 + line_height);

    context = CreateContext(&glist);
    if (!context) return;

    ok_gadget = create_button(gui, context, ok_left, ok_top, 88, 22, "OK", 201);
    if (!ok_gadget) goto cleanup;

    left = ((LONG)gui->screen->Width - width) / 2;
    top = ((LONG)gui->screen->Height - height) / 2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;

    {
        struct TagItem window_tags[11];
        int wi = 0;
        window_tags[wi].ti_Tag = WA_Left; window_tags[wi++].ti_Data = (ULONG)left;
        window_tags[wi].ti_Tag = WA_Top; window_tags[wi++].ti_Data = (ULONG)top;
        window_tags[wi].ti_Tag = WA_Width; window_tags[wi++].ti_Data = width;
        window_tags[wi].ti_Tag = WA_Height; window_tags[wi++].ti_Data = height;
        window_tags[wi].ti_Tag = WA_IDCMP; window_tags[wi++].ti_Data =
            IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW;
        window_tags[wi].ti_Tag = WA_Flags; window_tags[wi++].ti_Data =
            WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
            WFLG_ACTIVATE | WFLG_SIMPLE_REFRESH;
        window_tags[wi].ti_Tag = WA_Gadgets; window_tags[wi++].ti_Data = (ULONG)glist;
        window_tags[wi].ti_Tag = WA_Title; window_tags[wi++].ti_Data = (ULONG)"About AmiDrop";
        window_tags[wi].ti_Tag = WA_PubScreen; window_tags[wi++].ti_Data = (ULONG)gui->screen;
        window_tags[wi].ti_Tag = WA_AutoAdjust; window_tags[wi++].ti_Data = TRUE;
        window_tags[wi].ti_Tag = TAG_DONE; window_tags[wi].ti_Data = 0;
        window = OpenWindowTagList(NULL, window_tags);
    }
    if (!window) goto cleanup;

    GT_RefreshWindow(window, NULL);
    draw_about_contents(gui, window);

    while (!done) {
        Wait(1UL << window->UserPort->mp_SigBit);
        while ((message = GT_GetIMsg(window->UserPort)) != NULL) {
            ULONG msg_class = message->Class;
            struct Gadget *gadget = (struct Gadget *)message->IAddress;
            GT_ReplyIMsg(message);

            if (msg_class == IDCMP_CLOSEWINDOW) {
                done = TRUE;
            } else if (msg_class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(window);
                draw_about_contents(gui, window);
                GT_EndRefresh(window, TRUE);
            } else if (msg_class == IDCMP_GADGETUP &&
                       gadget && gadget->GadgetID == 201) {
                done = TRUE;
            }
        }
    }

cleanup:
    if (window) CloseWindow(window);
    if (glist) FreeGadgets(glist);
}

static BOOL choose_receive_dir_for_window(struct AmiDropGui *gui, struct Window *parent,
                                          char *dir, ULONG dir_size)
{
    struct FileRequester *requester;
    struct TagItem alloc_tags[1];
    struct TagItem request_tags[5];
    BOOL result = FALSE;

    if (!gui || !parent || !dir || dir_size == 0) return FALSE;
    alloc_tags[0].ti_Tag = TAG_DONE;
    alloc_tags[0].ti_Data = 0;
    requester = (struct FileRequester *)AllocAslRequest(ASL_FileRequest, alloc_tags);
    if (!requester) return FALSE;

    request_tags[0].ti_Tag = ASLFR_Window;
    request_tags[0].ti_Data = (ULONG)parent;
    request_tags[1].ti_Tag = ASLFR_TitleText;
    request_tags[1].ti_Data = (ULONG)"Choose AmiDrop receive folder";
    request_tags[2].ti_Tag = ASLFR_InitialDrawer;
    request_tags[2].ti_Data = (ULONG)dir;
    request_tags[3].ti_Tag = ASLFR_DrawersOnly;
    request_tags[3].ti_Data = TRUE;
    request_tags[4].ti_Tag = TAG_DONE;
    request_tags[4].ti_Data = 0;

    if (AslRequest(requester, request_tags)) {
        if (requester->fr_Drawer && requester->fr_Drawer[0]) {
            strncpy(dir, (const char *)requester->fr_Drawer, dir_size - 1);
            dir[dir_size - 1] = '\0';
            result = TRUE;
        }
    }
    FreeAslRequest(requester);
    return result;
}

static const ULONG size_values_kb[] = {
    512UL, 1024UL, 5120UL, 10240UL, 25600UL,
    51200UL, 102400UL, 256000UL, 512000UL, 1048576UL
};

static STRPTR size_labels[] = {
    (STRPTR)"512 KB", (STRPTR)"1 MB", (STRPTR)"5 MB", (STRPTR)"10 MB",
    (STRPTR)"25 MB", (STRPTR)"50 MB", (STRPTR)"100 MB", (STRPTR)"250 MB",
    (STRPTR)"500 MB", (STRPTR)"1 GB", NULL
};

static UWORD max_size_index(ULONG value)
{
    UWORD i;
    UWORD best = 0;
    unsigned long long best_diff = ~0ULL;
    for (i = 0; i < 10; ++i) {
        unsigned long long a = value;
        unsigned long long b = size_values_kb[i];
        unsigned long long diff = a > b ? a - b : b - a;
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best;
}

static void format_capacity(char *dst, size_t dst_size, unsigned long long bytes)
{
    if (!dst || dst_size == 0) return;
    if (bytes < 1048576ULL) {
        snprintf(dst, dst_size, "%lu KB", (unsigned long)(bytes / 1024ULL));
    } else if (bytes < 1073741824ULL) {
        unsigned long mb10 = (unsigned long)((bytes * 10ULL) / 1048576ULL);
        snprintf(dst, dst_size, "%lu.%lu MB", mb10 / 10UL, mb10 % 10UL);
    } else {
        unsigned long gb10 = (unsigned long)((bytes * 10ULL) / 1073741824ULL);
        snprintf(dst, dst_size, "%lu.%lu GB", gb10 / 10UL, gb10 % 10UL);
    }
}

static BOOL confirm_receive_capacity(struct Window *parent, const char *dir,
                                     ULONG limit_kb, BOOL *ignore_free_space)
{
    BOOL known = FALSE;
    unsigned long long free_bytes;
    unsigned long long limit_bytes;
    unsigned long long required;
    char free_text[32];
    char need_text[32];
    char body[384];
    struct EasyStruct easy;

    if (!ignore_free_space || *ignore_free_space) return TRUE;
    free_bytes = server_free_bytes_for_path(dir, &known);
    if (!known) return TRUE;

    limit_bytes = (unsigned long long)limit_kb * 1024ULL;
    required = server_required_free_bytes(limit_bytes);
    if (free_bytes >= required) return TRUE;

    format_capacity(free_text, sizeof(free_text), free_bytes);
    format_capacity(need_text, sizeof(need_text), required);
    snprintf(body, sizeof(body),
             "The selected volume has %s free.\n"
             "The selected file-size limit can require about %s including a safety reserve.\n\n"
             "Ignore the free-space check for this receive folder?",
             free_text, need_text);

    memset(&easy, 0, sizeof(easy));
    easy.es_StructSize = sizeof(easy);
    easy.es_Title = (STRPTR)"AmiDrop - low free space";
    easy.es_TextFormat = (STRPTR)body;
    easy.es_GadgetFormat = (STRPTR)"Ignore|Cancel";
    if (EasyRequestArgs(parent, &easy, NULL, NULL) == 1) {
        *ignore_free_space = TRUE;
        return TRUE;
    }
    return FALSE;
}

int gui_preferences(struct AmiDropGui *gui, struct AmiDropPrefs *prefs)
{
    struct Window *window = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *context;
    struct Gadget *last;
    struct Gadget *dir_gadget;
    struct Gadget *browse_gadget;
    struct Gadget *size_gadget;
    struct Gadget *port_gadget;
    struct Gadget *start_gadget;
    struct Gadget *ignore_gadget;
    struct Gadget *save_gadget;
    struct Gadget *use_gadget;
    struct Gadget *cancel_gadget;
    struct NewGadget ng;
    struct TagItem tags[4];
    struct IntuiMessage *message;
    LONG left;
    LONG top;
    UWORD size_index;
    BOOL done = FALSE;
    int action = PREFS_ACTION_CANCEL;
    char working_dir[AMIDROP_PATH_MAX];
    WORD width;
    WORD height;
    WORD field_left;
    WORD browse_width;
    WORD folder_width;
    WORD browse_left;
    WORD y_folder;
    WORD y_size;
    WORD y_port;
    WORD y_start;
    WORD y_ignore;
    WORD y_buttons;
    WORD button_gap;
    WORD button_width;
    WORD button_left;

    if (!gui || !gui->screen || !gui->visual_info || !prefs) return PREFS_ACTION_CANCEL;
    strncpy(working_dir, prefs->receive_dir, sizeof(working_dir) - 1);
    working_dir[sizeof(working_dir) - 1] = '\0';
    size_index = max_size_index(prefs->max_file_kb);

    width = 470;
    height = 236;
    if (gui->screen->Width < (UWORD)(width + 4))
        width = gui->screen->Width > 4 ? (WORD)(gui->screen->Width - 4) : (WORD)gui->screen->Width;
    if (gui->screen->Height < (UWORD)(height + 4)) {
        if (gui->screen->Height <= 220)
            height = gui->screen->Height > 8 ? (WORD)(gui->screen->Height - 8) : (WORD)gui->screen->Height;
        else
            height = gui->screen->Height > 4 ? (WORD)(gui->screen->Height - 4) : (WORD)gui->screen->Height;
    }

    field_left = width < 430 ? 112 : 130;
    browse_width = width < 380 ? 60 : 72;
    folder_width = (WORD)(width - field_left - browse_width - 24);
    if (folder_width < 70) folder_width = 70;
    browse_left = (WORD)(field_left + folder_width + 6);

    if (height <= 212) {
        /* 200-line Workbench: keep every gadget clear of the title and
           bottom borders instead of squeezing the normal Preferences layout. */
        y_folder = 18; y_size = 45; y_port = 72; y_start = 99; y_ignore = 120;
        y_buttons = (WORD)(height - 45);
    } else if (height < 225) {
        y_folder = 20; y_size = 51; y_port = 82; y_start = 111; y_ignore = 133;
        y_buttons = (WORD)(height - 37);
    } else {
        y_folder = 28; y_size = 62; y_port = 96; y_start = 128; y_ignore = 151;
        y_buttons = 188;
    }
    if (y_buttons < y_ignore + 24) y_buttons = (WORD)(y_ignore + 24);

    button_gap = width < 360 ? 5 : 10;
    button_left = width < 360 ? 10 : 24;
    button_width = (WORD)((width - button_left * 2 - button_gap * 2) / 3);

    context = CreateContext(&glist);
    if (!context) return PREFS_ACTION_CANCEL;
    last = context;

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = field_left; ng.ng_TopEdge = y_folder; ng.ng_Width = folder_width; ng.ng_Height = 18;
    ng.ng_GadgetText = (STRPTR)"Receive folder"; ng.ng_TextAttr = gui->screen->Font;
    ng.ng_GadgetID = 101;
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_VisualInfo = gui->visual_info;
    tags[0].ti_Tag = GTST_String; tags[0].ti_Data = (ULONG)working_dir;
    tags[1].ti_Tag = GTST_MaxChars; tags[1].ti_Data = AMIDROP_PATH_MAX - 1;
    tags[2].ti_Tag = TAG_DONE; tags[2].ti_Data = 0;
    dir_gadget = CreateGadgetA(STRING_KIND, last, &ng, tags);
    if (!dir_gadget) goto cleanup;
    last = dir_gadget;

    browse_gadget = create_button(gui, last, browse_left, (WORD)(y_folder - 2), browse_width, 22, "Browse...", 102);
    if (!browse_gadget) goto cleanup;
    last = browse_gadget;

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = field_left; ng.ng_TopEdge = y_size; ng.ng_Width = width < 360 ? 130 : 150; ng.ng_Height = 20;
    ng.ng_GadgetText = (STRPTR)"Max. file size"; ng.ng_TextAttr = gui->screen->Font;
    ng.ng_GadgetID = 103;
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_VisualInfo = gui->visual_info;
    tags[0].ti_Tag = GTCY_Labels; tags[0].ti_Data = (ULONG)size_labels;
    tags[1].ti_Tag = GTCY_Active; tags[1].ti_Data = size_index;
    tags[2].ti_Tag = TAG_DONE; tags[2].ti_Data = 0;
    size_gadget = CreateGadgetA(CYCLE_KIND, last, &ng, tags);
    if (!size_gadget) goto cleanup;
    last = size_gadget;

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = field_left; ng.ng_TopEdge = y_port; ng.ng_Width = 100; ng.ng_Height = 18;
    ng.ng_GadgetText = (STRPTR)"TCP port"; ng.ng_TextAttr = gui->screen->Font;
    ng.ng_GadgetID = 104;
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_VisualInfo = gui->visual_info;
    tags[0].ti_Tag = GTIN_Number; tags[0].ti_Data = prefs->port;
    tags[1].ti_Tag = GTIN_MaxChars; tags[1].ti_Data = 5;
    tags[2].ti_Tag = TAG_DONE; tags[2].ti_Data = 0;
    port_gadget = CreateGadgetA(INTEGER_KIND, last, &ng, tags);
    if (!port_gadget) goto cleanup;
    last = port_gadget;

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = field_left; ng.ng_TopEdge = y_start; ng.ng_Width = 26; ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"Start server on launch"; ng.ng_TextAttr = gui->screen->Font;
    ng.ng_GadgetID = 105;
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_VisualInfo = gui->visual_info;
    tags[0].ti_Tag = GTCB_Checked; tags[0].ti_Data = prefs->start_server;
    tags[1].ti_Tag = TAG_DONE; tags[1].ti_Data = 0;
    start_gadget = CreateGadgetA(CHECKBOX_KIND, last, &ng, tags);
    if (!start_gadget) goto cleanup;
    last = start_gadget;

    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = field_left; ng.ng_TopEdge = y_ignore; ng.ng_Width = 26; ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"Ignore free-space check"; ng.ng_TextAttr = gui->screen->Font;
    ng.ng_GadgetID = 109;
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_VisualInfo = gui->visual_info;
    tags[0].ti_Tag = GTCB_Checked; tags[0].ti_Data = prefs->ignore_free_space;
    tags[1].ti_Tag = TAG_DONE; tags[1].ti_Data = 0;
    ignore_gadget = CreateGadgetA(CHECKBOX_KIND, last, &ng, tags);
    if (!ignore_gadget) goto cleanup;
    last = ignore_gadget;

    save_gadget = create_button(gui, last, button_left, y_buttons, button_width, 22, "Save", 106);
    if (!save_gadget) goto cleanup;
    last = save_gadget;
    use_gadget = create_button(gui, last, (WORD)(button_left + button_width + button_gap), y_buttons, button_width, 22, "Use", 107);
    if (!use_gadget) goto cleanup;
    last = use_gadget;
    cancel_gadget = create_button(gui, last, (WORD)(button_left + (button_width + button_gap) * 2), y_buttons, button_width, 22, "Cancel", 108);
    if (!cancel_gadget) goto cleanup;

    left = ((LONG)gui->screen->Width - width) / 2;
    top = ((LONG)gui->screen->Height - height) / 2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;

    {
        struct TagItem window_tags[11];
        int wi = 0;
        window_tags[wi].ti_Tag = WA_Left; window_tags[wi++].ti_Data = (ULONG)left;
        window_tags[wi].ti_Tag = WA_Top; window_tags[wi++].ti_Data = (ULONG)top;
        window_tags[wi].ti_Tag = WA_Width; window_tags[wi++].ti_Data = width;
        window_tags[wi].ti_Tag = WA_Height; window_tags[wi++].ti_Data = height;
        window_tags[wi].ti_Tag = WA_IDCMP; window_tags[wi++].ti_Data = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW;
        window_tags[wi].ti_Tag = WA_Flags; window_tags[wi++].ti_Data = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIMPLE_REFRESH;
        window_tags[wi].ti_Tag = WA_Gadgets; window_tags[wi++].ti_Data = (ULONG)glist;
        window_tags[wi].ti_Tag = WA_Title; window_tags[wi++].ti_Data = (ULONG)"AmiDrop Preferences";
        window_tags[wi].ti_Tag = WA_PubScreen; window_tags[wi++].ti_Data = (ULONG)gui->screen;
        window_tags[wi].ti_Tag = WA_AutoAdjust; window_tags[wi++].ti_Data = TRUE;
        window_tags[wi].ti_Tag = TAG_DONE; window_tags[wi].ti_Data = 0;
        window = OpenWindowTagList(NULL, window_tags);
    }
    if (!window) goto cleanup;
    GT_RefreshWindow(window, NULL);

    while (!done) {
        Wait(1UL << window->UserPort->mp_SigBit);
        while ((message = GT_GetIMsg(window->UserPort)) != NULL) {
            ULONG msg_class = message->Class;
            UWORD code = message->Code;
            struct Gadget *gadget = (struct Gadget *)message->IAddress;
            GT_ReplyIMsg(message);

            if (msg_class == IDCMP_CLOSEWINDOW) {
                done = TRUE;
                action = PREFS_ACTION_CANCEL;
            } else if (msg_class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(window);
                GT_EndRefresh(window, TRUE);
            } else if (msg_class == IDCMP_GADGETUP && gadget) {
                if (gadget->GadgetID == 102) {
                    struct TagItem string_tags[2];
                    struct StringInfo *si = (struct StringInfo *)dir_gadget->SpecialInfo;
                    if (si && si->Buffer) {
                        strncpy(working_dir, (const char *)si->Buffer, sizeof(working_dir) - 1);
                        working_dir[sizeof(working_dir) - 1] = '\0';
                    }
                    if (choose_receive_dir_for_window(gui, window, working_dir, sizeof(working_dir))) {
                        string_tags[0].ti_Tag = GTST_String;
                        string_tags[0].ti_Data = (ULONG)working_dir;
                        string_tags[1].ti_Tag = TAG_DONE;
                        string_tags[1].ti_Data = 0;
                        GT_SetGadgetAttrsA(dir_gadget, window, NULL, string_tags);
                    }
                } else if (gadget->GadgetID == 103) {
                    if (code < 10) size_index = code;
                } else if (gadget->GadgetID == 106 || gadget->GadgetID == 107) {
                    struct StringInfo *dir_si = (struct StringInfo *)dir_gadget->SpecialInfo;
                    struct StringInfo *port_si = (struct StringInfo *)port_gadget->SpecialInfo;
                    LONG port_value = port_si ? port_si->LongInt : 0;
                    BOOL ignore_space = (ignore_gadget->Flags & GFLG_SELECTED) ? TRUE : FALSE;
                    if (!dir_si || !dir_si->Buffer || !dir_si->Buffer[0]) {
                        easy_message_window(window, "AmiDrop Preferences", "Please choose a receive folder.", "OK");
                        continue;
                    }
                    if (!prefs_valid_port((ULONG)port_value)) {
                        easy_message_window(window, "AmiDrop Preferences", "The TCP port must be between 1024 and 65535.", "OK");
                        continue;
                    }
                    if (size_index >= 10) size_index = 5;
                    if (!confirm_receive_capacity(window, (const char *)dir_si->Buffer,
                                                  size_values_kb[size_index], &ignore_space)) {
                        continue;
                    }
                    strncpy(prefs->receive_dir, (const char *)dir_si->Buffer, sizeof(prefs->receive_dir) - 1);
                    prefs->receive_dir[sizeof(prefs->receive_dir) - 1] = '\0';
                    prefs->port = (UWORD)port_value;
                    prefs->max_file_kb = size_values_kb[size_index];
                    prefs->start_server = (start_gadget->Flags & GFLG_SELECTED) ? TRUE : FALSE;
                    prefs->ignore_free_space = ignore_space;
                    action = gadget->GadgetID == 106 ? PREFS_ACTION_SAVE : PREFS_ACTION_USE;
                    done = TRUE;
                } else if (gadget->GadgetID == 108) {
                    action = PREFS_ACTION_CANCEL;
                    done = TRUE;
                }
            }
        }
    }

cleanup:
    if (window) CloseWindow(window);
    if (glist) FreeGadgets(glist);
    return action;
}
