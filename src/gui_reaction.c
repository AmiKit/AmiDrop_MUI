#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/utility.h>

#include <clib/alib_protos.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <proto/fuelgauge.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/getfile.h>
#include <proto/integer.h>
#include <proto/label.h>
#include <proto/requester.h>

#include <classes/window.h>
#include <classes/requester.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/getfile.h>
#include <gadgets/integer.h>
#include <images/label.h>

#include <stdio.h>
#include <string.h>

#include "amidrop.h"
#include "gui_reaction.h"
#include "prefs.h"
#include "server.h"

extern struct Library *GadToolsBase;

struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *ListBrowserBase = NULL;
struct Library *FuelGaugeBase = NULL;
struct Library *ChooserBase = NULL;
struct Library *CheckBoxBase = NULL;
struct Library *GetFileBase = NULL;
struct Library *IntegerBase = NULL;
struct Library *LabelBase = NULL;
struct Library *RequesterBase = NULL;

#define REACTION_MIN_VERSION 47
#define QR_PIXELS 148
#define PREF_GID_FOLDER 101
#define PREF_GID_SIZE   102
#define PREF_GID_PORT   103
#define PREF_GID_START  104
#define PREF_GID_SAVE   105
#define PREF_GID_USE    106
#define PREF_GID_CANCEL 107
#define PREF_GID_IGNORE 108
#define ABOUT_GID_OK    201

static struct NewMenu main_menu_template[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Start server", NULL, 0, 0, (APTR)MID_START_SERVER },
    { NM_ITEM,  (STRPTR)"Stop server", NULL, 0, 0, (APTR)MID_STOP_SERVER },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Preferences...", NULL, 0, 0, (APTR)MID_PREFS },
    { NM_ITEM,  NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"About AmiDrop...", NULL, 0, 0, (APTR)MID_ABOUT },
    { NM_ITEM,  (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MID_QUIT },
    { NM_END,   NULL, NULL, 0, 0, NULL }
};

static void close_reaction_libraries(void);

static void list_init(struct List *list)
{
    if (!list) return;
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}

static BOOL open_reaction_libraries(void)
{
    WindowBase = OpenLibrary((STRPTR)"window.class", REACTION_MIN_VERSION);
    LayoutBase = OpenLibrary((STRPTR)"gadgets/layout.gadget", REACTION_MIN_VERSION);
    ButtonBase = OpenLibrary((STRPTR)"gadgets/button.gadget", REACTION_MIN_VERSION);
    ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", REACTION_MIN_VERSION);
    FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", REACTION_MIN_VERSION);
    ChooserBase = OpenLibrary((STRPTR)"gadgets/chooser.gadget", REACTION_MIN_VERSION);
    CheckBoxBase = OpenLibrary((STRPTR)"gadgets/checkbox.gadget", REACTION_MIN_VERSION);
    GetFileBase = OpenLibrary((STRPTR)"gadgets/getfile.gadget", REACTION_MIN_VERSION);
    IntegerBase = OpenLibrary((STRPTR)"gadgets/integer.gadget", REACTION_MIN_VERSION);
    LabelBase = OpenLibrary((STRPTR)"images/label.image", REACTION_MIN_VERSION);
    RequesterBase = OpenLibrary((STRPTR)"requester.class", REACTION_MIN_VERSION);

    if (!WindowBase || !LayoutBase || !ButtonBase || !ListBrowserBase ||
        !FuelGaugeBase || !ChooserBase || !CheckBoxBase || !GetFileBase ||
        !IntegerBase || !LabelBase || !RequesterBase) {
        close_reaction_libraries();
        return FALSE;
    }
    return TRUE;
}

static void close_reaction_libraries(void)
{
    if (RequesterBase) { CloseLibrary(RequesterBase); RequesterBase = NULL; }
    if (LabelBase) { CloseLibrary(LabelBase); LabelBase = NULL; }
    if (IntegerBase) { CloseLibrary(IntegerBase); IntegerBase = NULL; }
    if (GetFileBase) { CloseLibrary(GetFileBase); GetFileBase = NULL; }
    if (CheckBoxBase) { CloseLibrary(CheckBoxBase); CheckBoxBase = NULL; }
    if (ChooserBase) { CloseLibrary(ChooserBase); ChooserBase = NULL; }
    if (FuelGaugeBase) { CloseLibrary(FuelGaugeBase); FuelGaugeBase = NULL; }
    if (ListBrowserBase) { CloseLibrary(ListBrowserBase); ListBrowserBase = NULL; }
    if (ButtonBase) { CloseLibrary(ButtonBase); ButtonBase = NULL; }
    if (LayoutBase) { CloseLibrary(LayoutBase); LayoutBase = NULL; }
    if (WindowBase) { CloseLibrary(WindowBase); WindowBase = NULL; }
}

static Object *label_image(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)text,
        TAG_END);
}

static Object *text_line(const char *text, WORD justification)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE,
        GA_Text, (ULONG)(text ? text : ""),
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,
        BUTTON_Justification, justification,
        TAG_END);
}

static ULONG pen_brightness(struct Screen *screen, UWORD pen)
{
    ULONG rgb;
    if (!screen || !screen->ViewPort.ColorMap) return 0;
    rgb = GetRGB4(screen->ViewPort.ColorMap, pen);
    return ((rgb >> 8) & 15UL) + ((rgb >> 4) & 15UL) + (rgb & 15UL);
}

static void choose_qr_pens(struct AmiDropGui *gui, UWORD *dark, UWORD *light)
{
    struct DrawInfo *draw_info;
    UWORD candidates[5];
    ULONG darkest = ~0UL;
    ULONG lightest = 0;
    int i;

    if (!dark || !light) return;
    *dark = 1;
    *light = 0;
    if (!gui || !gui->screen) return;

    draw_info = GetScreenDrawInfo(gui->screen);
    if (!draw_info) return;
    candidates[0] = draw_info->dri_Pens[TEXTPEN];
    candidates[1] = draw_info->dri_Pens[BACKGROUNDPEN];
    candidates[2] = draw_info->dri_Pens[SHINEPEN];
    candidates[3] = draw_info->dri_Pens[SHADOWPEN];
    candidates[4] = draw_info->dri_Pens[FILLPEN];

    for (i = 0; i < 5; ++i) {
        ULONG value = pen_brightness(gui->screen, candidates[i]);
        if (value < darkest) { darkest = value; *dark = candidates[i]; }
        if (value > lightest) { lightest = value; *light = candidates[i]; }
    }
    FreeScreenDrawInfo(gui->screen, draw_info);
}

static void clear_qr_bitmap(struct AmiDropGui *gui)
{
    struct RastPort rp;
    UWORD dark;
    UWORD light;
    if (!gui || !gui->qr_bitmap) return;
    choose_qr_pens(gui, &dark, &light);
    InitRastPort(&rp);
    rp.BitMap = gui->qr_bitmap;
    SetAPen(&rp, light);
    RectFill(&rp, 0, 0, QR_PIXELS - 1, QR_PIXELS - 1);
}

static BOOL update_qr_data(struct AmiDropGui *gui, const struct AmiDropServer *server)
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

static void render_qr_bitmap(struct AmiDropGui *gui)
{
    struct RastPort rp;
    UWORD dark;
    UWORD light;
    const WORD scale = 4;
    const WORD quiet = 4;
    uint8_t y;

    if (!gui || !gui->qr_bitmap) return;
    choose_qr_pens(gui, &dark, &light);
    InitRastPort(&rp);
    rp.BitMap = gui->qr_bitmap;
    SetAPen(&rp, light);
    RectFill(&rp, 0, 0, QR_PIXELS - 1, QR_PIXELS - 1);

    if (gui->qr_valid) {
        SetAPen(&rp, dark);
        for (y = 0; y < gui->qr.size; ++y) {
            uint8_t x;
            for (x = 0; x < gui->qr.size; ++x) {
                if (qrcode_getModule(&gui->qr, x, y)) {
                    WORD x1 = (WORD)((x + quiet) * scale);
                    WORD y1 = (WORD)((y + quiet) * scale);
                    RectFill(&rp, x1, y1,
                             (WORD)(x1 + scale - 1),
                             (WORD)(y1 + scale - 1));
                }
            }
        }
    }

    if (gui->window && gui->qr_obj) {
        RefreshGList((struct Gadget *)gui->qr_obj, gui->window, NULL, 1);
    }
    gui->qr_force_redraw = FALSE;
}

static BOOL create_menu(struct AmiDropGui *gui)
{
    struct TagItem layout_tags[2];
    if (!gui || !gui->screen || !GadToolsBase) return FALSE;
    gui->visual_info = GetVisualInfoA(gui->screen, NULL);
    if (!gui->visual_info) return FALSE;
    gui->menu = CreateMenusA(main_menu_template, NULL);
    if (!gui->menu) return FALSE;
    layout_tags[0].ti_Tag = GTMN_NewLookMenus;
    layout_tags[0].ti_Data = TRUE;
    layout_tags[1].ti_Tag = TAG_DONE;
    layout_tags[1].ti_Data = 0;
    if (!LayoutMenusA(gui->menu, gui->visual_info, layout_tags)) return FALSE;
    return TRUE;
}

BOOL gui_open(struct AmiDropGui *gui)
{
    ULONG sigmask = 0;
    if (!gui) return FALSE;
    memset(gui, 0, sizeof(*gui));
    list_init(&gui->history_list);
    gui->history_list_ready = TRUE;

    if (!open_reaction_libraries()) return FALSE;
    gui->screen = LockPubScreen(NULL);
    if (!gui->screen) goto fail;
    if (!create_menu(gui)) goto fail;

    gui->qr_bitmap = AllocBitMap(QR_PIXELS, QR_PIXELS,
                                 gui->screen->RastPort.BitMap->Depth,
                                 BMF_CLEAR, gui->screen->RastPort.BitMap);
    if (!gui->qr_bitmap) goto fail;
    clear_qr_bitmap(gui);

    gui->window_obj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"AmiDrop " AMIDROP_VERSION " - ReAction",
        WA_Width, 540,
        WA_Height, 405,
        WA_MinWidth, 470,
        WA_MinHeight, 330,
        WA_MaxWidth, 8192,
        WA_MaxHeight, 8192,
        WA_PubScreen, (ULONG)gui->screen,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_DragBar, TRUE,
        WA_Activate, TRUE,
        WA_AutoAdjust, TRUE,
        WA_NoCareRefresh, TRUE,
        WA_NewLookMenus, TRUE,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_NEWSIZE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_GadgetHelp, TRUE,

        WINDOW_ParentGroup, gui->root_layout = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_LeftSpacing, 4,
            LAYOUT_RightSpacing, 4,
            LAYOUT_TopSpacing, 4,
            LAYOUT_BottomSpacing, 4,

            LAYOUT_AddChild, NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_SpaceInner, TRUE,

                LAYOUT_AddChild, NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_SpaceInner, TRUE,

                    LAYOUT_AddChild, gui->address_obj = text_line("Address: Server not running", BCJ_LEFT),
                    CHILD_MinHeight, 16,
                    LAYOUT_AddChild, gui->code_obj = text_line("Code: ------ (PC)", BCJ_LEFT),
                    CHILD_MinHeight, 16,
                    LAYOUT_AddChild, gui->receive_obj = text_line("Receive: -", BCJ_LEFT),
                    CHILD_MinHeight, 16,
                    LAYOUT_AddChild, gui->status_obj = text_line("Status: Starting...", BCJ_LEFT),
                    CHILD_MinHeight, 16,
                    LAYOUT_AddChild, gui->limit_obj = text_line("Limit: 50 MB/file", BCJ_LEFT),
                    CHILD_MinHeight, 16,
                    LAYOUT_AddChild, gui->progress_obj = NewObject(FUELGAUGE_GetClass(), NULL,
                        FUELGAUGE_Min, 0,
                        FUELGAUGE_Max, 100,
                        FUELGAUGE_Level, 0,
                        FUELGAUGE_Ticks, 0,
                        FUELGAUGE_Percent, TRUE,
                        FUELGAUGE_FillPen, FILLPEN,
                        TAG_END),
                    CHILD_MinHeight, 20,
                    TAG_END),
                CHILD_WeightedWidth, 100,

                LAYOUT_AddChild, NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, gui->qr_obj = NewObject(BUTTON_GetClass(), NULL,
                        GA_ReadOnly, TRUE,
                        BUTTON_BitMap, (ULONG)gui->qr_bitmap,
                        BUTTON_BevelStyle, BVS_NONE,
                        TAG_END),
                    CHILD_MinWidth, QR_PIXELS,
                    CHILD_MaxWidth, QR_PIXELS,
                    CHILD_MinHeight, QR_PIXELS,
                    CHILD_MaxHeight, QR_PIXELS,
                    LAYOUT_AddChild, text_line("Scan with phone", BCJ_CENTER),
                    CHILD_MinHeight, 16,
                    TAG_END),
                CHILD_MinWidth, QR_PIXELS,
                CHILD_MaxWidth, QR_PIXELS,
                CHILD_WeightedWidth, 0,
                TAG_END),
            CHILD_WeightedHeight, 55,

            LAYOUT_AddChild, text_line("Successfully received (newest first)", BCJ_LEFT),
            CHILD_MinHeight, 16,

            LAYOUT_AddChild, gui->history_obj = NewObject(LISTBROWSER_GetClass(), NULL,
                GA_ID, 10,
                GA_ReadOnly, TRUE,
                GA_TabCycle, TRUE,
                LISTBROWSER_Labels, (ULONG)&gui->history_list,
                LISTBROWSER_ShowSelected, FALSE,
                TAG_END),
            CHILD_WeightedHeight, 45,

            LAYOUT_AddChild, NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, gui->abort_obj = NewObject(BUTTON_GetClass(), NULL,
                    GA_ID, RGUI_GID_ABORT,
                    GA_Text, (ULONG)"Abort transfer",
                    GA_RelVerify, TRUE,
                    GA_Disabled, TRUE,
                    TAG_END),
                CHILD_WeightedWidth, 1,
                LAYOUT_AddChild, gui->clear_obj = NewObject(BUTTON_GetClass(), NULL,
                    GA_ID, RGUI_GID_CLEAR,
                    GA_Text, (ULONG)"Clear list",
                    GA_RelVerify, TRUE,
                    TAG_END),
                CHILD_WeightedWidth, 1,
                LAYOUT_AddChild, gui->quit_obj = NewObject(BUTTON_GetClass(), NULL,
                    GA_ID, RGUI_GID_QUIT,
                    GA_Text, (ULONG)"Quit",
                    GA_RelVerify, TRUE,
                    TAG_END),
                CHILD_WeightedWidth, 1,
                TAG_END),
            CHILD_MinHeight, 24,
            TAG_END),
        TAG_END);

    if (!gui->window_obj) goto fail;
    gui->window = (struct Window *)RA_OpenWindow(gui->window_obj);
    if (!gui->window) goto fail;
    if (gui->menu) SetMenuStrip(gui->window, gui->menu);
    GetAttr(WINDOW_SigMask, gui->window_obj, &sigmask);
    gui->signal_mask = sigmask;
    gui->qr_force_redraw = TRUE;
    render_qr_bitmap(gui);
    return TRUE;

fail:
    gui_close(gui);
    return FALSE;
}

void gui_close(struct AmiDropGui *gui)
{
    if (!gui) return;
    if (gui->window && gui->menu) ClearMenuStrip(gui->window);
    if (gui->history_obj && gui->window) {
        SetGadgetAttrs((struct Gadget *)gui->history_obj, gui->window, NULL,
                       LISTBROWSER_Labels, ~0UL, TAG_END);
    }
    if (gui->window_obj) {
        DisposeObject(gui->window_obj);
        gui->window_obj = NULL;
        gui->window = NULL;
    }
    if (gui->history_list_ready) {
        FreeListBrowserList(&gui->history_list);
        list_init(&gui->history_list);
        gui->history_list_ready = FALSE;
    }
    if (gui->qr_bitmap) {
        FreeBitMap(gui->qr_bitmap);
        gui->qr_bitmap = NULL;
    }
    if (gui->menu) {
        FreeMenus(gui->menu);
        gui->menu = NULL;
    }
    if (gui->visual_info) {
        FreeVisualInfo(gui->visual_info);
        gui->visual_info = NULL;
    }
    if (gui->screen) {
        UnlockPubScreen(NULL, gui->screen);
        gui->screen = NULL;
    }
    close_reaction_libraries();
}

void gui_force_qr_redraw(struct AmiDropGui *gui)
{
    if (gui) gui->qr_force_redraw = TRUE;
}

void gui_redraw(struct AmiDropGui *gui, const struct AmiDropServer *server,
                const struct AmiDropPrefs *prefs)
{
    char line[384];
    ULONG percent = 0;
    BOOL qr_changed;

    if (!gui || !gui->window || !server || !prefs) return;

    snprintf(line, sizeof(line), "Status: %s", server->status);
    SetGadgetAttrs((struct Gadget *)gui->status_obj, gui->window, NULL,
                   GA_Text, (ULONG)line, TAG_END);

    snprintf(line, sizeof(line), "Address: %s", server->address[0] ? server->address : "Server not running");
    SetGadgetAttrs((struct Gadget *)gui->address_obj, gui->window, NULL,
                   GA_Text, (ULONG)line, TAG_END);

    snprintf(line, sizeof(line), "Code: %s (PC)", server->access_code[0] ? server->access_code : "------");
    SetGadgetAttrs((struct Gadget *)gui->code_obj, gui->window, NULL,
                   GA_Text, (ULONG)line, TAG_END);

    snprintf(line, sizeof(line), "Receive: %s", prefs->receive_dir);
    SetGadgetAttrs((struct Gadget *)gui->receive_obj, gui->window, NULL,
                   GA_Text, (ULONG)line, TAG_END);

    if (prefs->max_file_kb < 1024UL)
        snprintf(line, sizeof(line), "Limit: %lu KB/file", (unsigned long)prefs->max_file_kb);
    else if (prefs->max_file_kb < 1048576UL)
        snprintf(line, sizeof(line), "Limit: %lu MB/file", (unsigned long)(prefs->max_file_kb / 1024UL));
    else
        snprintf(line, sizeof(line), "Limit: 1 GB/file");
    SetGadgetAttrs((struct Gadget *)gui->limit_obj, gui->window, NULL,
                   GA_Text, (ULONG)line, TAG_END);

    if (server->uploading && server->upload_total > 0) {
        percent = (ULONG)(((unsigned long long)server->upload_received * 100ULL) / server->upload_total);
        if (percent > 100UL) percent = 100UL;
    } else if (strncmp(server->status, "Received:", 9) == 0) {
        percent = 100UL;
    }
    SetGadgetAttrs((struct Gadget *)gui->progress_obj, gui->window, NULL,
                   FUELGAUGE_Level, percent, TAG_END);

    qr_changed = update_qr_data(gui, server);
    if (qr_changed || gui->qr_force_redraw) render_qr_bitmap(gui);
}

void gui_sync_history(struct AmiDropGui *gui, const struct AmiDropServer *server)
{
    UWORD i;
    if (!gui || !server || !gui->history_obj || !gui->window) return;
    if (gui->history_generation == server->transfer_generation) return;

    SetGadgetAttrs((struct Gadget *)gui->history_obj, gui->window, NULL,
                   LISTBROWSER_Labels, ~0UL, TAG_END);
    FreeListBrowserList(&gui->history_list);
    list_init(&gui->history_list);

    for (i = 0; i < server->transfer_count; ++i) {
        struct TagItem node_tags[4];
        struct Node *node;
        node_tags[0].ti_Tag = LBNA_Column;
        node_tags[0].ti_Data = 0;
        node_tags[1].ti_Tag = LBNCA_CopyText;
        node_tags[1].ti_Data = TRUE;
        node_tags[2].ti_Tag = LBNCA_Text;
        node_tags[2].ti_Data = (ULONG)server->transfers[i].display;
        node_tags[3].ti_Tag = TAG_DONE;
        node_tags[3].ti_Data = 0;
        node = AllocListBrowserNodeA(1, node_tags);
        if (node) AddTail(&gui->history_list, node);
    }

    SetGadgetAttrs((struct Gadget *)gui->history_obj, gui->window, NULL,
                   LISTBROWSER_Labels, (ULONG)&gui->history_list,
                   LISTBROWSER_Top, 0,
                   TAG_END);
    gui->history_generation = server->transfer_generation;
}

void gui_set_abort_enabled(struct AmiDropGui *gui, BOOL enabled)
{
    if (!gui || !gui->abort_obj || !gui->window) return;
    SetGadgetAttrs((struct Gadget *)gui->abort_obj, gui->window, NULL,
                   GA_Disabled, !enabled, TAG_END);
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

BOOL gui_next_event(struct AmiDropGui *gui, struct AmiDropGuiEvent *event)
{
    ULONG result;
    WORD code = 0;
    if (!event) return FALSE;
    event->type = RGUI_EVENT_NONE;
    event->value = 0;
    if (!gui || !gui->window_obj) return FALSE;

    while ((result = RA_HandleInput(gui->window_obj, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                event->type = RGUI_EVENT_QUIT;
                return TRUE;
            case WMHI_MENUPICK:
                event->type = RGUI_EVENT_MENU;
                event->value = (ULONG)(UWORD)code;
                return TRUE;
            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                    case RGUI_GID_ABORT: event->type = RGUI_EVENT_ABORT; return TRUE;
                    case RGUI_GID_CLEAR: event->type = RGUI_EVENT_CLEAR; return TRUE;
                    case RGUI_GID_QUIT: event->type = RGUI_EVENT_QUIT; return TRUE;
                    default: break;
                }
                break;
            case WMHI_NEWSIZE:
                break;
            default:
                break;
        }
    }
    return FALSE;
}

static ULONG show_requester(struct AmiDropGui *gui, const char *title, const char *body,
                            const char *gadgets, ULONG image)
{
    Object *requester;
    struct orRequest request;
    ULONG result = 0;
    if (!RequesterBase) return 0;

    requester = NewObject(REQUESTER_GetClass(), NULL,
        REQ_Type, REQTYPE_INFO,
        REQ_TitleText, (ULONG)(title ? title : "AmiDrop"),
        REQ_BodyText, (ULONG)(body ? body : ""),
        REQ_GadgetText, (ULONG)(gadgets ? gadgets : "OK"),
        REQ_Image, image,
        TAG_DONE);
    if (!requester) return 0;

    request.MethodID = RM_OPENREQ;
    request.or_Attrs = NULL;
    request.or_Window = gui ? gui->window : NULL;
    request.or_Screen = gui ? gui->screen : NULL;
    result = DoMethodA(requester, (Msg)&request);
    DisposeObject(requester);
    return result;
}

void gui_message(struct AmiDropGui *gui, const char *title, const char *text)
{
    if (RequesterBase) {
        show_requester(gui, title, text, "OK", REQIMAGE_INFO);
    } else {
        struct EasyStruct easy;
        easy.es_StructSize = sizeof(easy);
        easy.es_Flags = 0;
        easy.es_Title = (STRPTR)(title ? title : "AmiDrop");
        easy.es_TextFormat = (STRPTR)(text ? text : "");
        easy.es_GadgetFormat = (STRPTR)"OK";
        EasyRequestArgs(gui ? gui->window : NULL, &easy, NULL, NULL);
    }
}

BOOL gui_confirm(struct AmiDropGui *gui, const char *title, const char *text,
                 const char *yes_text, const char *no_text)
{
    char buttons[96];
    snprintf(buttons, sizeof(buttons), "%s|%s",
             yes_text ? yes_text : "Yes", no_text ? no_text : "No");
    if (RequesterBase) {
        return show_requester(gui, title, text, buttons, REQIMAGE_QUESTION) != 0;
    } else {
        struct EasyStruct easy;
        easy.es_StructSize = sizeof(easy);
        easy.es_Flags = 0;
        easy.es_Title = (STRPTR)(title ? title : "AmiDrop");
        easy.es_TextFormat = (STRPTR)(text ? text : "");
        easy.es_GadgetFormat = (STRPTR)buttons;
        return EasyRequestArgs(gui ? gui->window : NULL, &easy, NULL, NULL) != 0;
    }
}

static UWORD max_size_index(ULONG value)
{
    static const ULONG values[] = {
        512UL, 1024UL, 5120UL, 10240UL, 25600UL,
        51200UL, 102400UL, 256000UL, 512000UL, 1048576UL
    };
    UWORD i;
    UWORD best = 0;
    unsigned long long best_diff = ~0ULL;
    for (i = 0; i < 10; ++i) {
        unsigned long long a = value;
        unsigned long long b = values[i];
        unsigned long long diff = a > b ? a - b : b - a;
        if (diff < best_diff) { best_diff = diff; best = i; }
    }
    return best;
}

static BOOL build_size_list(struct List *list)
{
    static const char *labels[] = {
        "512 KB", "1 MB", "5 MB", "10 MB", "25 MB",
        "50 MB", "100 MB", "250 MB", "500 MB", "1 GB", NULL
    };
    int i;
    list_init(list);
    for (i = 0; labels[i]; ++i) {
        struct TagItem node_tags[2];
        struct Node *node;
        node_tags[0].ti_Tag = CNA_Text;
        node_tags[0].ti_Data = (ULONG)labels[i];
        node_tags[1].ti_Tag = TAG_DONE;
        node_tags[1].ti_Data = 0;
        node = AllocChooserNodeA(node_tags);
        if (!node) {
            struct Node *old_node;
            while ((old_node = RemHead(list)) != NULL) FreeChooserNode(old_node);
            list_init(list);
            return FALSE;
        }
        AddTail(list, node);
    }
    return TRUE;
}

static void free_size_list(struct List *list)
{
    struct Node *node;
    if (!list) return;
    while ((node = RemHead(list)) != NULL) FreeChooserNode(node);
    list_init(list);
}

int gui_preferences(struct AmiDropGui *gui, struct AmiDropPrefs *prefs)
{
    static const ULONG size_values[] = {
        512UL, 1024UL, 5120UL, 10240UL, 25600UL,
        51200UL, 102400UL, 256000UL, 512000UL, 1048576UL
    };
    struct List size_list;
    Object *window_obj = NULL;
    Object *folder_obj = NULL;
    Object *size_obj = NULL;
    Object *port_obj = NULL;
    Object *start_obj = NULL;
    Object *ignore_obj = NULL;
    struct Window *window = NULL;
    BOOL done = FALSE;
    int action = PREFS_ACTION_CANCEL;
    char working_dir[AMIDROP_PATH_MAX];
    UWORD initial_size;

    if (!gui || !gui->screen || !prefs) return PREFS_ACTION_CANCEL;
    if (!build_size_list(&size_list)) return PREFS_ACTION_CANCEL;
    strncpy(working_dir, prefs->receive_dir, sizeof(working_dir) - 1);
    working_dir[sizeof(working_dir) - 1] = '\0';
    initial_size = max_size_index(prefs->max_file_kb);

    window_obj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"AmiDrop Preferences",
        WA_PubScreen, (ULONG)gui->screen,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_Activate, TRUE,
        WA_AutoAdjust, TRUE,
        WA_NoCareRefresh, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,
        WINDOW_ParentGroup, NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_LeftSpacing, 4,
            LAYOUT_RightSpacing, 4,
            LAYOUT_TopSpacing, 4,
            LAYOUT_BottomSpacing, 4,

            LAYOUT_AddChild, folder_obj = NewObject(GETFILE_GetClass(), NULL,
                GA_ID, PREF_GID_FOLDER,
                GA_RelVerify, TRUE,
                GETFILE_TitleText, (ULONG)"Choose AmiDrop receive folder",
                GETFILE_Drawer, (ULONG)working_dir,
                GETFILE_DoSaveMode, FALSE,
                GETFILE_DrawersOnly, TRUE,
                GETFILE_ReadOnly, TRUE,
                TAG_END),
            CHILD_Label, label_image("Receive folder"),

            LAYOUT_AddChild, size_obj = NewObject(CHOOSER_GetClass(), NULL,
                GA_ID, PREF_GID_SIZE,
                GA_RelVerify, TRUE,
                GA_TabCycle, TRUE,
                CHOOSER_PopUp, TRUE,
                CHOOSER_Labels, (ULONG)&size_list,
                CHOOSER_Selected, initial_size,
                CHOOSER_AutoFit, TRUE,
                TAG_END),
            CHILD_Label, label_image("Max. file size"),

            LAYOUT_AddChild, port_obj = NewObject(INTEGER_GetClass(), NULL,
                GA_ID, PREF_GID_PORT,
                GA_RelVerify, TRUE,
                GA_TabCycle, TRUE,
                INTEGER_Number, (LONG)prefs->port,
                INTEGER_MaxChars, 5,
                INTEGER_Minimum, 1024,
                INTEGER_Maximum, 65535,
                INTEGER_Arrows, FALSE,
                INTEGER_MinVisible, 5,
                TAG_END),
            CHILD_Label, label_image("TCP port"),

            LAYOUT_AddChild, start_obj = NewObject(CHECKBOX_GetClass(), NULL,
                GA_ID, PREF_GID_START,
                GA_RelVerify, TRUE,
                GA_Text, (ULONG)"Start server on launch",
                GA_Selected, prefs->start_server,
                TAG_END),

            LAYOUT_AddChild, ignore_obj = NewObject(CHECKBOX_GetClass(), NULL,
                GA_ID, PREF_GID_IGNORE,
                GA_RelVerify, TRUE,
                GA_Text, (ULONG)"Ignore free-space check",
                GA_Selected, prefs->ignore_free_space,
                TAG_END),

            LAYOUT_AddChild, NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, NewObject(BUTTON_GetClass(), NULL,
                    GA_ID, PREF_GID_SAVE,
                    GA_Text, (ULONG)"Save",
                    GA_RelVerify, TRUE,
                    TAG_END),
                CHILD_WeightedWidth, 1,
                LAYOUT_AddChild, NewObject(BUTTON_GetClass(), NULL,
                    GA_ID, PREF_GID_USE,
                    GA_Text, (ULONG)"Use",
                    GA_RelVerify, TRUE,
                    TAG_END),
                CHILD_WeightedWidth, 1,
                LAYOUT_AddChild, NewObject(BUTTON_GetClass(), NULL,
                    GA_ID, PREF_GID_CANCEL,
                    GA_Text, (ULONG)"Cancel",
                    GA_RelVerify, TRUE,
                    TAG_END),
                CHILD_WeightedWidth, 1,
                TAG_END),
            CHILD_MinHeight, 24,
            TAG_END),
        TAG_END);

    if (!window_obj || !folder_obj || !size_obj || !port_obj || !start_obj || !ignore_obj) goto cleanup;
    window = (struct Window *)RA_OpenWindow(window_obj);
    if (!window) goto cleanup;

    while (!done) {
        ULONG sigmask = 0;
        ULONG result;
        WORD code = 0;
        GetAttr(WINDOW_SigMask, window_obj, &sigmask);
        {
            ULONG wait_mask = Wait(sigmask | SIGBREAKF_CTRL_C);
            if (wait_mask & SIGBREAKF_CTRL_C) {
                done = TRUE;
                action = PREFS_ACTION_CANCEL;
            }
        }
        while (!done && (result = RA_HandleInput(window_obj, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
                done = TRUE;
                action = PREFS_ACTION_CANCEL;
            } else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP) {
                ULONG gid = result & WMHI_GADGETMASK;
                if (gid == PREF_GID_FOLDER) {
                    if (DoMethod(folder_obj, GFILE_REQUEST, window)) {
                        STRPTR drawer = NULL;
                        GetAttr(GETFILE_Drawer, folder_obj, (ULONG *)&drawer);
                        if (drawer && drawer[0]) {
                            strncpy(working_dir, (const char *)drawer, sizeof(working_dir) - 1);
                            working_dir[sizeof(working_dir) - 1] = '\0';
                        }
                    }
                } else if (gid == PREF_GID_CANCEL) {
                    done = TRUE;
                    action = PREFS_ACTION_CANCEL;
                } else if (gid == PREF_GID_USE || gid == PREF_GID_SAVE) {
                    ULONG selected = 0;
                    ULONG port = 0;
                    ULONG checked = 0;
                    ULONG ignore_space = 0;
                    STRPTR drawer = NULL;
                    GetAttr(GETFILE_Drawer, folder_obj, (ULONG *)&drawer);
                    GetAttr(CHOOSER_Selected, size_obj, &selected);
                    GetAttr(INTEGER_Number, port_obj, &port);
                    GetAttr(GA_Selected, start_obj, &checked);
                    GetAttr(GA_Selected, ignore_obj, &ignore_space);
                    if (!drawer || !drawer[0]) {
                        gui_message(gui, "AmiDrop Preferences", "Please choose a receive folder.");
                    } else if (!prefs_valid_port(port)) {
                        gui_message(gui, "AmiDrop Preferences", "TCP port must be between 1024 and 65535.");
                    } else {
                        if (selected > 9) selected = 5;
                        strncpy(prefs->receive_dir, (const char *)drawer, sizeof(prefs->receive_dir) - 1);
                        prefs->receive_dir[sizeof(prefs->receive_dir) - 1] = '\0';
                        prefs->max_file_kb = size_values[selected];
                        prefs->port = (UWORD)port;
                        prefs->start_server = checked ? TRUE : FALSE;
                        prefs->ignore_free_space = ignore_space ? TRUE : FALSE;
                        action = (gid == PREF_GID_SAVE) ? PREFS_ACTION_SAVE : PREFS_ACTION_USE;
                        done = TRUE;
                    }
                }
            }
        }
    }

cleanup:
    if (window_obj) DisposeObject(window_obj);
    free_size_list(&size_list);
    return action;
}

void gui_show_about(struct AmiDropGui *gui)
{
    Object *window_obj;
    struct Window *window;
    BOOL done = FALSE;
    if (!gui || !gui->screen) return;

    window_obj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"About AmiDrop",
        WA_PubScreen, (ULONG)gui->screen,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_Activate, TRUE,
        WA_AutoAdjust, TRUE,
        WA_NoCareRefresh, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,
        WINDOW_ParentGroup, NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, text_line("AmiDrop " AMIDROP_VERSION " - ReAction", BCJ_CENTER),
            LAYOUT_AddChild, text_line("File transfer for AmigaOS", BCJ_CENTER),
            LAYOUT_AddChild, text_line("(c) 2026 Andreas 'Andiweli' St\374rmer", BCJ_CENTER),
            LAYOUT_AddChild, text_line("QR code generator: Richard Moore / Project Nayuki", BCJ_CENTER),
            LAYOUT_AddChild, text_line("MIT licensed", BCJ_CENTER),
            LAYOUT_AddChild, NewObject(BUTTON_GetClass(), NULL,
                GA_ID, ABOUT_GID_OK,
                GA_Text, (ULONG)"OK",
                GA_RelVerify, TRUE,
                TAG_END),
            CHILD_MinWidth, 80,
            CHILD_MaxWidth, 120,
            TAG_END),
        TAG_END);
    if (!window_obj) return;
    window = (struct Window *)RA_OpenWindow(window_obj);
    if (!window) { DisposeObject(window_obj); return; }

    while (!done) {
        ULONG sigmask = 0;
        ULONG result;
        WORD code = 0;
        GetAttr(WINDOW_SigMask, window_obj, &sigmask);
        {
            ULONG wait_mask = Wait(sigmask | SIGBREAKF_CTRL_C);
            if (wait_mask & SIGBREAKF_CTRL_C) done = TRUE;
        }
        while (!done && (result = RA_HandleInput(window_obj, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) done = TRUE;
            else if ((result & WMHI_CLASSMASK) == WMHI_GADGETUP &&
                     (result & WMHI_GADGETMASK) == ABOUT_GID_OK) done = TRUE;
        }
    }
    DisposeObject(window_obj);
}
