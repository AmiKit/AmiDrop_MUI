/* MUI frontend for AmiDrop.
 *
 * MUI does the layout, so there is no window geometry arithmetic here: no
 * hand drawn progress bar, no hand managed list, no manual placement.  Two
 * things did not come for free.  The QR code has no MUI class, so this file
 * carries a small private Area subclass - and that subclass does pick its
 * canvas from the screen size, mirroring the GadTools frontend, because MUI
 * cannot know how large a scannable symbol has to be.  The redraw path also
 * keeps a small cache of what is currently displayed, because the server
 * marks itself dirty for every received network block.
 *
 * Requires MUI 3.8 (muimaster.library 19) or newer.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/classes.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <libraries/mui.h>
#include <libraries/asl.h>
#include <libraries/iffparse.h>
#include <libraries/gadtools.h>   /* NM_BARLABEL for the menu separators */

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <string.h>

#include "amidrop.h"
#include "clipboard.h"
#include "gui.h"
#include "prefs.h"
#include "server.h"
#include "qrcode.h"
#include "util.h"

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

struct Library *MUIMasterBase = NULL;

/* MUI 3.8 is muimaster.library 19.  The MUI 5 headers default MUIMASTER_VMIN
   to 20, which would lock out 3.8 for no reason: everything used here is
   documented as V4 or V11. */
#define AMIDROP_MUIMASTER_VERSION 19

/* The dispatcher of a private custom class is a plain function - not a hook -
   and MUI calls it with the class in A0, the object in A2 and the message in
   A1 (muimaster.library/MUI_CreateCustomClass). */
#if defined(__VBCC__)
#define DISPATCHER_ARGS __reg("a0") struct IClass *cl, __reg("a2") Object *obj, \
                        __reg("a1") Msg msg
#elif defined(__GNUC__)
#define DISPATCHER_ARGS struct IClass *cl __asm("a0"), Object *obj __asm("a2"), \
                        Msg msg __asm("a1")
#else
#error "Unsupported compiler: no register argument syntax for the MUI dispatcher."
#endif

/* ------------------------------------------------------------------ */
/* QR code class                                                       */
/* ------------------------------------------------------------------ */

/* The four-module quiet zone the QR standard requires around the symbol. */
#define QR_QUIET 4

/* Cells across a version 3 symbol plus both quiet zones; sizes the edge
   tables in qr_draw().  A version N symbol is 4*N+17 modules square. */
#define QR_MAX_CELLS ((4 * AMIDROP_QR_VERSION + 17) + QR_QUIET * 2)

#define MUIA_AmiDropQR_Code  (TAG_USER | 0x0AD00001) /* const QRCode *  */
#define MUIA_AmiDropQR_Valid (TAG_USER | 0x0AD00002) /* BOOL            */

/* muimaster.library/MUI_Redraw is explicit that it may only be called from
   inside a dispatcher, so the application asks for a repaint by sending this
   method rather than by calling MUI_Redraw itself.  It is also needed when
   only the symbol content changed, which no attribute would report. */
#define MUIM_AmiDropQR_Changed (TAG_USER | 0x0AD00010)

struct QrData {
    const QRCode *code;
    BOOL valid;
};

static struct MUI_CustomClass *qr_mcc = NULL;

/* _screen(), _win() and _rp() all dereference mad_RenderInfo, which exists
   only between MUIM_Setup and MUIM_Cleanup.  Checking the result is too late
   - the dereference has already happened - so test the pointer itself. */
#define QR_IS_SETUP(obj) (muiRenderInfo(obj) != NULL)

/* Classic Amiga screen modes use non-square pixels, so a QR drawn as a
   square raster looks vertically stretched on a 4:3 display.  Ask for the
   compensating width instead; modern modes are left alone because their
   physical aspect cannot be derived from the raster size. */
static LONG qr_corrected_width(Object *obj, LONG height)
{
    struct Screen *screen;
    LONG corrected;

    if (height <= 0 || !QR_IS_SETUP(obj)) return height;
    screen = _screen(obj);
    if (!screen) return height;
    if (screen->Width > 720 || screen->Height > 400) return height;

    corrected = ((LONG)height * 3L * (LONG)screen->Width +
                 2L * (LONG)screen->Height) / (4L * (LONG)screen->Height);
    return corrected > height ? corrected : height;
}

static ULONG qr_new(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct QrData *data;

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (!obj) return 0;

    data = INST_DATA(cl, obj);
    data->code = (const QRCode *)GetTagData(MUIA_AmiDropQR_Code, 0, msg->ops_AttrList);
    data->valid = (BOOL)GetTagData(MUIA_AmiDropQR_Valid, FALSE, msg->ops_AttrList);
    return (ULONG)obj;
}

static ULONG qr_set(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct QrData *data = INST_DATA(cl, obj);
    struct TagItem *tags = msg->ops_AttrList;
    struct TagItem *tag;
    BOOL changed = FALSE;

    while ((tag = NextTagItem(&tags)) != NULL) {
        switch (tag->ti_Tag) {
            case MUIA_AmiDropQR_Code:
                data->code = (const QRCode *)tag->ti_Data;
                changed = TRUE;
                break;
            case MUIA_AmiDropQR_Valid:
                if (data->valid != (BOOL)tag->ti_Data) {
                    data->valid = (BOOL)tag->ti_Data;
                    changed = TRUE;
                }
                break;
            default:
                break;
        }
    }

    /* No MUI_Redraw here: the only writer of these attributes is
       update_qr(), and it always follows the set with MUIM_AmiDropQR_Changed,
       which repaints.  Redrawing on the set as well painted the object twice
       on every server start and stop. */
    (void)changed;
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static ULONG qr_changed(struct IClass *cl, Object *obj)
{
    (void)cl;
    if (QR_IS_SETUP(obj) && _win(obj)) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return 0;
}

static ULONG qr_ask_min_max(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    /* MUI asks for the size once, during Setup, which happens while the
       window is opened - long before update_qr() ever runs.  Reading
       data->code->size here would read a zeroed struct and lay the object
       out for 8 cells instead of 37.  The symbol version is fixed, so derive
       the size from it: a version N symbol is 4*N+17 modules square. */
    LONG cells = (4 * AMIDROP_QR_VERSION + 17) + QR_QUIET * 2;
    /* Pick the canvas exactly the way the GadTools frontend does
       (gui.c/configure_main_geometry): 148 pixels normally, 111 on a screen
       too short for its full layout, 74 on a very low one.  With 37 cells
       those are cells*4, cells*3 and cells*2.  Its thresholds are on the
       WINDOW height, which it derives from the screen; the equivalent screen
       heights are used here because MUI asks for the size before there is a
       window.  Minimum, default and maximum are all the same value, so the
       symbol is a fixed canvas like the original rather than something the
       layout can stretch. */
    LONG canvas = cells * 4;
    struct Screen *screen = QR_IS_SETUP(obj) ? _screen(obj) : NULL;

    if (screen) {
        if (screen->Height <= 220) canvas = cells * 2;
        else if (screen->Height < 324) canvas = cells * 3;
    }

    DoSuperMethodA(cl, obj, (Msg)msg);

    msg->MinMaxInfo->MinWidth  += (WORD)qr_corrected_width(obj, canvas);
    msg->MinMaxInfo->DefWidth  += (WORD)qr_corrected_width(obj, canvas);
    msg->MinMaxInfo->MaxWidth  += (WORD)qr_corrected_width(obj, canvas);

    msg->MinMaxInfo->MinHeight += (WORD)canvas;
    msg->MinMaxInfo->DefHeight += (WORD)canvas;
    msg->MinMaxInfo->MaxHeight += (WORD)canvas;
    return 0;
}

static ULONG qr_draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    struct QrData *data = INST_DATA(cl, obj);
    struct RastPort *rp;
    LONG left, top, width, height, cells;
    LONG draw_w, draw_h;
    uint8_t x, y;

    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & MADF_DRAWOBJECT)) return 0;

    if (!QR_IS_SETUP(obj)) return 0;
    rp = _rp(obj);
    if (!rp) return 0;
    left = _mleft(obj);
    top = _mtop(obj);
    width = _mwidth(obj);
    height = _mheight(obj);
    if (width <= 0 || height <= 0) return 0;

    /* White paper, so a phone camera sees the expected contrast whatever the
       MUI background looks like. */
    SetAPen(rp, _dri(obj)->dri_Pens[SHINEPEN]);
    RectFill(rp, left, top, left + width - 1, top + height - 1);

    if (!data->valid || !data->code) return 0;

    cells = data->code->size + QR_QUIET * 2;

    /* The box MUI hands us is whatever the layout produced, and its width and
       height come from opposite ends of the min/max range - stretching the
       modules across it would make them rectangular and unscannable.  So the
       symbol is fitted INSIDE the box at the right aspect and centred, the
       way the GadTools frontend has always drawn it into its fixed canvas.
       That makes the drawing independent of the layout, which is what four
       rounds of adjusting layout numbers failed to achieve. */
    draw_h = height;
    draw_w = qr_corrected_width(obj, draw_h);
    if (draw_w > width) {
        /* Too wide for the box: scale the height down proportionally and
           re-derive the width, so the aspect is preserved. */
        draw_h = (height * width) / draw_w;
        if (draw_h < 1) draw_h = 1;
        draw_w = qr_corrected_width(obj, draw_h);
        if (draw_w > width) draw_w = width;
    }
    left += (width - draw_w) / 2;
    top += (height - draw_h) / 2;

    SetAPen(rp, _dri(obj)->dri_Pens[SHADOWPEN]);

    /* Map module coordinates onto that square proportionally, so no module is
       lost to rounding whatever size it came out.  The edges are computed once
       per axis rather than four times per dark module: a version 3 symbol has
       roughly 400 dark modules, which was over 1600 divides on a 68000 build
       where the divide is a software routine.  Now it is 2 * (cells + 1). */
    {
        WORD edge_x[QR_MAX_CELLS + 1];
        WORD edge_y[QR_MAX_CELLS + 1];
        LONG i;

        for (i = 0; i <= cells; ++i) {
            edge_x[i] = (WORD)(left + (i * draw_w) / cells);
            edge_y[i] = (WORD)(top + (i * draw_h) / cells);
        }

        for (y = 0; y < data->code->size; ++y) {
            WORD y1 = edge_y[y + QR_QUIET];
            WORD y2 = (WORD)(edge_y[y + QR_QUIET + 1] - 1);
            if (y2 < y1) continue;
            for (x = 0; x < data->code->size; ++x) {
                if (qrcode_getModule((QRCode *)data->code, x, y)) {
                    WORD x1 = edge_x[x + QR_QUIET];
                    WORD x2 = (WORD)(edge_x[x + QR_QUIET + 1] - 1);
                    if (x2 >= x1) RectFill(rp, x1, y1, x2, y2);
                }
            }
        }
    }
    return 0;
}

static ULONG qr_dispatcher(DISPATCHER_ARGS)
{
    switch (msg->MethodID) {
        case OM_NEW:         return qr_new(cl, obj, (APTR)msg);
        case OM_SET:         return qr_set(cl, obj, (APTR)msg);
        case MUIM_AskMinMax: return qr_ask_min_max(cl, obj, (APTR)msg);
        case MUIM_Draw:      return qr_draw(cl, obj, (APTR)msg);
        case MUIM_AmiDropQR_Changed: return qr_changed(cl, obj);
    }
    return DoSuperMethodA(cl, obj, msg);
}

/* ------------------------------------------------------------------ */
/* Frontend state                                                      */
/* ------------------------------------------------------------------ */

/* Return IDs handed back by MUIM_Application_NewInput.  MUIV_Application_-
   ReturnID_Quit is -1, so any positive value is free for us. */
#define RID_QUIT        1
#define RID_ABORT       2
#define RID_CLEAR       3
#define RID_COPY        4
#define RID_MENU_BASE   16          /* RID_MENU_BASE + MID_ code */

/* MUI keeps ONE application-wide fifo of return IDs
   (MUI_Application.doc/MUIM_Application_ReturnID), so these must not collide
   with the main window's - otherwise a Quit click is read as Save. */
#define PREFS_RID_SAVE   32
#define PREFS_RID_USE    33
#define PREFS_RID_CANCEL 34

struct AmiDropGui {
    Object *app;
    Object *window;
    Object *address_text;
    Object *code_text;
    Object *receive_text;
    Object *status_text;
    Object *limit_text;
    Object *qr_object;
    Object *qr_label;
    Object *gauge;
    Object *history_view;
    Object *list;
    Object *copy_button;
    Object *abort_button;
    Object *clear_button;
    Object *quit_button;

    /* The preferences window is a second subwindow, built once and simply
       opened and closed.  MUIA_Application_Window is initialise-only, so a
       window created later could not be attached to the application. */
    Object *prefs_window;
    Object *prefs_dir;
    Object *prefs_pop;
    Object *prefs_port;
    Object *prefs_size;
    Object *prefs_start;
    Object *prefs_ignore;
    Object *prefs_show_info;
    Object *prefs_save;
    Object *prefs_use;
    Object *prefs_cancel;

    ULONG signal_mask;
    ULONG pending_signals;

    /* Last values pushed into the display, so an unchanged server state does
       not make MUI re-render text it already shows. */
    char shown_address[128];
    char shown_code[32];
    char shown_receive[AMIDROP_PATH_MAX + 16];
    char shown_status[192];
    char shown_limit[64];
    LONG shown_percent;
    ULONG shown_limit_kb;

    ULONG history_generation;

    /* Whether a transfer is running, as last reported by
       gui_set_abort_enabled().  gui_next_event() needs it and, unlike
       gui_redraw(), is handed no server to ask - which is the whole reason
       this field exists. */
    BOOL uploading;

    /* Mirrors prefs->show_transfer_information for the life of this window.
       MUI could switch the layout live - MUIA_ShowMe is [ISG] and relayouts
       immediately (MUI_Area.doc) - but main.c applies the change by rebuilding
       the interface, which is the one route its contract offers all three
       frontends.  So within one window this value never changes. */
    BOOL show_transfer_information;

    QRCode qr;
    UBYTE qr_modules[AMIDROP_QR_BUFFER_SIZE];
    char qr_payload[96];
    BOOL qr_valid;

    /* What the Copy button puts on the clipboard: the same link the QR symbol
       carries.  Kept separately from qr_payload, which is written only after
       qrcode_initText() succeeds - an encoder failure must not also take the
       Copy button down with it. */
    char copy_url[96];
};

static struct AmiDropGui the_gui;
static BOOL the_gui_in_use = FALSE;

/* A quit that arrives while the Preferences dialog is running has to survive
   the interface being rebuilt.  MUI keeps return ids on a fifo owned by the
   application object (MUI_Application.doc), and applying a layout change
   disposes exactly that object - so an id put back there would die with it and
   AmiDrop would carry on running, still holding its single-instance port and
   so unable to be restarted.  Kept out here, it survives. */
static BOOL the_pending_quit = FALSE;

static const ULONG size_values_kb[] = {
    512UL, 1024UL, 5120UL, 10240UL, 25600UL,
    51200UL, 102400UL, 256000UL, 512000UL, 1048576UL
};

static const char *const size_labels[] = {
    "512 KB", "1 MB", "5 MB", "10 MB", "25 MB",
    "50 MB", "100 MB", "250 MB", "500 MB", "1 GB", NULL
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

static void format_limit_text(char *dst, size_t dst_size, ULONG max_file_kb)
{
    if (!dst || dst_size == 0) return;
    if (max_file_kb < 1024UL)
        snprintf(dst, dst_size, "%lu KB/file", (unsigned long)max_file_kb);
    else if (max_file_kb < 1048576UL)
        snprintf(dst, dst_size, "%lu MB/file", (unsigned long)(max_file_kb / 1024UL));
    else
        snprintf(dst, dst_size, "1 GB/file");
}

/* Only touch the object when the text really changed. */
static void set_text(Object *obj, char *cache, size_t cache_size, const char *text)
{
    if (!obj || !cache || cache_size == 0) return;
    if (!text) text = "";
    if (strncmp(cache, text, cache_size - 1) == 0) return;
    strncpy(cache, text, cache_size - 1);
    cache[cache_size - 1] = '\0';
    set(obj, MUIA_Text_Contents, (ULONG)cache);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static Object *labelled_row(const char *label, Object **value_out)
{
    Object *value;
    Object *row;

    value = TextObject,
        MUIA_Text_Contents, (ULONG)"",
        MUIA_Text_PreParse, (ULONG)"\33l",
        /* These carry a path and a status line.  With the default SetMin the
           window's minimum width would grow to whatever is displayed - up to
           255 characters of receive path. */
        MUIA_Text_SetMin, FALSE,
    End;
    if (!value) return NULL;

    row = HGroup,
        MUIA_Group_Spacing, 4,
        Child, Label2((STRPTR)label),
        Child, value,
    End;

    /* A failed group has already disposed the text object, so do not hand
       back a pointer to freed memory. */
    *value_out = row ? value : NULL;
    return row;
}

static Object *build_menu(void)
{
    return MenustripObject,
        Child, MenuObjectT((STRPTR)"Project"),
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)"Start server",
                MUIA_Menuitem_Shortcut, (ULONG)"S",
                MUIA_UserData, RID_MENU_BASE + MID_START_SERVER,
            End,
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)"Stop server",
                MUIA_Menuitem_Shortcut, (ULONG)"T",
                MUIA_UserData, RID_MENU_BASE + MID_STOP_SERVER,
            End,
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)NM_BARLABEL,
            End,
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)"Preferences...",
                MUIA_Menuitem_Shortcut, (ULONG)"P",
                MUIA_UserData, RID_MENU_BASE + MID_PREFS,
            End,
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)NM_BARLABEL,
            End,
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)"About AmiDrop...",
                MUIA_UserData, RID_MENU_BASE + MID_ABOUT,
            End,
            Child, MenuitemObject,
                MUIA_Menuitem_Title, (ULONG)"Quit",
                MUIA_Menuitem_Shortcut, (ULONG)"Q",
                MUIA_UserData, RID_MENU_BASE + MID_QUIT,
            End,
        End,
    End;
}

static Object *build_prefs_window(struct AmiDropGui *gui)
{
    return WindowObject,
        MUIA_Window_Title, (ULONG)"AmiDrop Preferences",
        /* Bumped from ADPR for the same reason as the main window ID. */
        MUIA_Window_ID   , MAKE_ID('A','D','P','5'),
        /* Open wider than the 350-pixel minimum: the label column is fixed,
           so every extra pixel goes to the string column - without this the
           window opens at its minimum and the receive folder field ends up
           a few characters wide (410 = 460 minus 50, Jan 2026-09-01). */
        MUIA_Window_Width, 410,

        WindowContents, VGroup,

            Child, VGroup,
                GroupFrameT((STRPTR)"Settings"),

                /* Only the two wide controls live in the label/control
                   columns.  The port field and the checkmarks are compact,
                   so each sits in a row of its own, pushed to the right
                   edge together with its label (Jan).  This also keeps
                   fixed-size objects out of the shared column - a column
                   maximum is the MINIMUM of its childrens maximums
                   (MUI_Group.doc), so one fixed checkmark there would pin
                   the whole field column to its minimum width forever. */
                Child, ColGroup(2),
                    Child, Label2((STRPTR)"Receive folder"),
                    Child, gui->prefs_pop = PopaslObject,
                        MUIA_Popstring_String, gui->prefs_dir = StringObject,
                            StringFrame,
                            MUIA_String_MaxLen, AMIDROP_PATH_MAX,
                        End,
                        MUIA_Popstring_Button, PopButton(MUII_PopDrawer),
                        MUIA_Popasl_Type, ASL_FileRequest,
                        ASLFR_DrawersOnly, TRUE,
                        ASLFR_TitleText, (ULONG)"Choose AmiDrop receive folder",
                    End,

                    Child, Label2((STRPTR)"Max. file size"),
                    Child, gui->prefs_size = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)size_labels,
                    End,
                End,

                Child, HGroup,
                    Child, HSpace(0),
                    Child, Label2((STRPTR)"TCP port"),
                    Child, gui->prefs_port = StringObject,
                        StringFrame,
                        MUIA_String_Accept, (ULONG)"0123456789",
                        MUIA_String_MaxLen, 6,
                        /* six characters wide, font independent */
                        MUIA_FixWidthTxt, (ULONG)"000000",
                    End,
                End,

                Child, HGroup,
                    Child, HSpace(0),
                    Child, Label2((STRPTR)"Start server on launch"),
                    Child, gui->prefs_start = MUI_MakeObject(MUIO_Checkmark, (ULONG)0),
                End,

                Child, HGroup,
                    Child, HSpace(0),
                    Child, Label2((STRPTR)"Ignore free-space check"),
                    Child, gui->prefs_ignore = MUI_MakeObject(MUIO_Checkmark, (ULONG)0),
                End,

                Child, HGroup,
                    Child, HSpace(0),
                    Child, Label2((STRPTR)"Show transfer information"),
                    Child, gui->prefs_show_info = MUI_MakeObject(MUIO_Checkmark, (ULONG)0),
                End,
            End,

            Child, HGroup,
                Child, gui->prefs_save   = SimpleButton("Save"),
                Child, gui->prefs_use    = SimpleButton("Use"),
                Child, gui->prefs_cancel = SimpleButton("Cancel"),
            End,

            /* Same minimum-width idiom as the main window, 350 pixels - and
               the same stretchable space, or the fixed rectangle would cap
               the window and silently clamp MUIA_Window_Width back to 350. */
            Child, HGroup,
                MUIA_Group_Spacing, 0,
                Child, RectangleObject,
                    MUIA_FixHeight, 1,
                    MUIA_FixWidth, 350,
                End,
                Child, HSpace(0),
            End,
        End,
    End;
}

static BOOL build_application(struct AmiDropGui *gui)
{
    /* gui->show_transfer_information is set before we are called.  MUIA_ShowMe
       defaults to TRUE (MUI_Area.doc), so an object that has to start hidden
       says so in its own tag list rather than in a set() afterwards - a later
       set is what once let an optimisation quietly unhide the QR label. */
    ULONG show_info = gui->show_transfer_information ? TRUE : FALSE;
    Object *menustrip;
    Object *address_row, *code_row, *receive_row, *status_row, *limit_row;

    menustrip = build_menu();

    /* Colons written out: MUIO_Label takes the text verbatim (mui.h has no
       flag that adds one), and the GadTools and ReAction windows both show
       "Address:", so this one was the odd one out.  ESC b turns on the bold
       soft style (MUI_Text.doc/MUIA_Text_Contents).  It lives in the literals
       rather than being composed into a buffer: no autodoc states whether
       MUIO_Label copies the string it is handed - only the existence of
       MUIO_Label_DontCopy in mui.h implies it - and a string constant makes
       the question moot. */
    address_row = labelled_row("\33bAddress:", &gui->address_text);
    /* The Copy button belongs beside what it copies, not down with Abort and
       Quit.  Wrapping the finished row keeps labelled_row() free of special
       cases; a failed group disposes its children, so drop both pointers with
       it. */
    if (address_row) {
        address_row = HGroup,
            MUIA_Group_Spacing, 4,
            Child, address_row,
            Child, gui->copy_button = SimpleButton("Copy to Clipboard"),
        End;
        if (!address_row) {
            gui->address_text = NULL;
            gui->copy_button = NULL;
        }
    }
    code_row    = labelled_row("\33bCode:", &gui->code_text);
    receive_row = labelled_row("\33bReceive:", &gui->receive_text);
    status_row  = labelled_row("\33bStatus:", &gui->status_text);
    limit_row   = labelled_row("\33bLimit:", &gui->limit_text);

    gui->qr_object = NewObject(qr_mcc->mcc_Class, NULL,
        MUIA_Frame, MUIV_Frame_ImageButton,
        MUIA_AmiDropQR_Code, (ULONG)&gui->qr,
        MUIA_AmiDropQR_Valid, FALSE,
        TAG_DONE);

    gui->prefs_window = build_prefs_window(gui);

    /* Once these are handed to the application below, MUI owns them: a
       failure anywhere in that call disposes the whole tree (MUI_Group.doc).
       Up to this point they are still ours, so bail out by hand. */
    if (!menustrip || !address_row || !code_row || !receive_row ||
        !status_row || !limit_row || !gui->qr_object || !gui->prefs_window) {
        if (gui->prefs_window) MUI_DisposeObject(gui->prefs_window);
        if (gui->qr_object) MUI_DisposeObject(gui->qr_object);
        if (limit_row) MUI_DisposeObject(limit_row);
        if (status_row) MUI_DisposeObject(status_row);
        if (receive_row) MUI_DisposeObject(receive_row);
        if (code_row) MUI_DisposeObject(code_row);
        if (address_row) MUI_DisposeObject(address_row);
        if (menustrip) MUI_DisposeObject(menustrip);
        gui->prefs_window = NULL;
        gui->qr_object = NULL;
        return FALSE;
    }

    gui->app = ApplicationObject,
        MUIA_Application_Title      , (ULONG)AMIDROP_NAME,
        MUIA_Application_Version    , (ULONG)("$VER: " AMIDROP_MUI_NAME " " AMIDROP_VERSION " (" AMIDROP_MUI_DATE ")"),
        MUIA_Application_Copyright  , (ULONG)"MIT licensed",
        MUIA_Application_Author     , (ULONG)"Andiweli",
        MUIA_Application_Description, (ULONG)"Receives files from a phone or PC over the network",
        MUIA_Application_Base       , (ULONG)"AMIDROP",

        SubWindow, gui->window = WindowObject,
            MUIA_Window_Title, (ULONG)(AMIDROP_NAME " " AMIDROP_VERSION " - file receiver"),
            /* Bumped once, ADRP -> ADR3, when the default HEIGHT changed:
               MUI remembers geometry per ID and a remembered size overrides
               a new default (MUI_Window.doc/MUIA_Window_Width).  The width
               below needs no second bump: this window sets no default width
               at all, only a minimum, so there is no default for a
               remembered size to override.  That a remembered width is then
               widened to a raised minimum follows from MUI's layout model
               and is stated in no autodoc I could find - if the window ever
               comes up narrow after a width change, this is the assumption
               that broke.  Bumping again would throw away everyone's
               remembered position and height. */
            MUIA_Window_ID   , MAKE_ID('A','D','R','3'),
            /* Open at minimum height - the list can always be pulled larger. */
            MUIA_Window_Height, MUIV_Window_Height_MinMax(0),
            MUIA_Window_Menustrip, (ULONG)menustrip,

            WindowContents, VGroup,

                Child, HGroup,
                    Child, VGroup,
                        Child, address_row,
                        Child, code_row,
                        Child, receive_row,
                        Child, status_row,
                        Child, limit_row,
                        Child, VSpace(0),
                    End,
                    Child, VGroup,
                        /* No MUIA_Weight, 0 here: MUI_Area.doc says a weight
                           of 0 pins an object at its MINIMUM for ever, which
                           made every Def/Max size computed in qr_ask_min_max
                           unreachable and left the symbol at 2 pixels per
                           module.  The QR's own MaxWidth caps this column
                           instead, so it cannot steal the list's space. */
                        Child, gui->qr_object,
                        /* SetMax FALSE: otherwise this label's width becomes
                           the column's maximum and squeezes the symbol. */
                        Child, gui->qr_label = TextObject,
                            MUIA_Text_Contents, (ULONG)"\33cScan to send",
                            MUIA_Text_SetMax, FALSE,
                            /* MUIA_ShowMe defaults to TRUE (MUI_Area.doc), and
                               no QR exists yet at this point.  Relying on the
                               first update_qr() pass to hide it was fragile:
                               its early-out returns before the set when no
                               address is known, which is exactly the state
                               this label must not be visible in. */
                            MUIA_ShowMe, FALSE,
                        End,
                        Child, VSpace(0),
                    End,
                End,

                Child, gui->gauge = GaugeObject,
                    GaugeFrame,
                    MUIA_Gauge_Horiz, TRUE,
                    MUIA_Gauge_Max, 100,
                    MUIA_Gauge_Current, 0,
                    /* Gauge itself substitutes MUIA_Gauge_Current for %ld
                       (MUI_Gauge.doc) - never set a transient buffer here. */
                    MUIA_Gauge_InfoText, (ULONG)"%ld %%",
                End,

                /* The transfer-information section: this list and the
                   Clear-list button beside Abort and Quit.  Both are hidden
                   together when the preference is off, as in the GadTools
                   frontend, where neither gadget is created at all. */
                Child, gui->history_view = ListviewObject,
                    MUIA_ShowMe, show_info,
                    MUIA_Listview_Input, FALSE,
                    MUIA_Listview_List, gui->list = ListObject,
                        InputListFrame,
                        MUIA_List_Title, (ULONG)"Successfully received (newest first)",
                        /* Without a construct hook List stores the pointer it
                           is given (MUI_List.doc/MUIM_List_InsertSingle), which
                           would alias server->transfers[] while the server
                           keeps rewriting it.  These two make MUI copy the
                           string and free its copy. */
                        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                        MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                    End,
                End,

                Child, HGroup,
                    Child, gui->abort_button = SimpleButton("Abort transfer"),
                    Child, gui->clear_button = SimpleButton("Clear list"),
                    Child, gui->quit_button  = SimpleButton("Quit"),
                End,

                /* MUI has no minimum-window-width attribute, so a fixed-width
                   one-pixel rectangle imposes one.  It must NOT stand alone in
                   the vertical group: a column's maximum width is the MINIMUM
                   of its childrens' maximums (MUI_Group.doc), so a bare fixed
                   rectangle would also cap the window there.  The stretchable
                   space beside it keeps the maximum unlimited.
                   540, up from 500: the value texts carry MUIA_Text_SetMin
                   FALSE (see labelled_row), so a row that does not fit is
                   clipped instead of widening the window, and the status line
                   was being cut off.  The figure comes from looking at it on
                   the Amiga, not from a calculation. */
                Child, HGroup,
                    MUIA_Group_Spacing, 0,
                    Child, RectangleObject,
                        MUIA_FixHeight, 1,
                        MUIA_FixWidth, 540,
                    End,
                    Child, HSpace(0),
                End,
            End,
        End,

        SubWindow, gui->prefs_window,
    End;

    return gui->app ? TRUE : FALSE;
}

static void attach_notifications(struct AmiDropGui *gui)
{
    ULONG code;

    DoMethod(gui->window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             gui->app, 2, MUIM_Application_ReturnID, RID_QUIT);
    DoMethod(gui->quit_button, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, RID_QUIT);
    DoMethod(gui->abort_button, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, RID_ABORT);
    DoMethod(gui->clear_button, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, RID_CLEAR);
    DoMethod(gui->copy_button, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, RID_COPY);

    DoMethod(gui->prefs_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             gui->app, 2, MUIM_Application_ReturnID, PREFS_RID_CANCEL);
    DoMethod(gui->prefs_cancel, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, PREFS_RID_CANCEL);
    DoMethod(gui->prefs_use, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, PREFS_RID_USE);
    DoMethod(gui->prefs_save, MUIM_Notify, MUIA_Pressed, FALSE,
             gui->app, 2, MUIM_Application_ReturnID, PREFS_RID_SAVE);

    /* Menu items report through their MUIA_UserData, which already carries
       the return ID we want. */
    for (code = MID_START_SERVER; code <= MID_ABOUT; ++code) {
        Object *item = (Object *)DoMethod(gui->app, MUIM_FindUData,
                                          RID_MENU_BASE + code);
        if (item) {
            DoMethod(item, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
                     gui->app, 2, MUIM_Application_ReturnID,
                     RID_MENU_BASE + code);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void gui_shutdown(struct AmiDropGui *gui)
{
    if (gui && gui->app) {
        set(gui->window, MUIA_Window_Open, FALSE);
        MUI_DisposeObject(gui->app);
        gui->app = NULL;
        gui->window = NULL;
    }
    if (qr_mcc) {
        MUI_DeleteCustomClass(qr_mcc);
        qr_mcc = NULL;
    }
    if (MUIMasterBase) {
        CloseLibrary(MUIMasterBase);
        MUIMasterBase = NULL;
    }
}

struct AmiDropGui *gui_create(const struct AmiDropPrefs *prefs)
{
    struct AmiDropGui *gui = &the_gui;
    ULONG open = FALSE;

    if (the_gui_in_use || !prefs) return NULL;
    memset(gui, 0, sizeof(*gui));
    gui->show_transfer_information = prefs->show_transfer_information;
    gui->history_generation = ~0UL;
    gui->shown_percent = -1;
    gui->shown_limit_kb = ~0UL;      /* nothing shown yet */

    MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, AMIDROP_MUIMASTER_VERSION);
    if (!MUIMasterBase) return NULL;

    qr_mcc = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct QrData),
                                   (APTR)qr_dispatcher);
    if (!qr_mcc) {
        gui_shutdown(gui);
        return NULL;
    }
    qr_mcc->mcc_Class->cl_ID = (ClassID)"AmiDropQR";

    if (!build_application(gui)) {
        gui_shutdown(gui);
        return NULL;
    }

    attach_notifications(gui);

    /* The list hid itself in its own tag list; SimpleButton() takes no tags
       (mui.h), so the button has to be told here.  Either way it happens
       before the window opens, so the layout is settled without them: the
       minimum height drops along with the list, and
       MUIV_Window_Height_MinMax(0) opens the window at exactly that minimum -
       the lower height the setting promises.  A size MUI already remembers
       for this window ID still wins, as it does for any MUI window. */
    set(gui->clear_button, MUIA_ShowMe, gui->show_transfer_information);

    set(gui->window, MUIA_Window_Open, TRUE);
    get(gui->window, MUIA_Window_Open, &open);
    if (!open) {
        gui_shutdown(gui);
        return NULL;
    }

    /* Nothing is being received yet - the other two frontends disable this
       button at open time and so must this one. */
    gui_set_abort_enabled(gui, FALSE);

    /* No server, no link: MUIA_Disabled defaults to FALSE, so the Copy button
       has to be switched off explicitly.  gui_redraw() switches it back on as
       soon as there is something to copy. */
    set(gui->copy_button, MUIA_Disabled, TRUE);

    /* Prime the signal mask; from here on every NewInput call refreshes it. */
    {
        ULONG primed = 0;
        DoMethod(gui->app, MUIM_Application_NewInput, &primed);
        gui->signal_mask = primed;
    }
    gui->pending_signals = 0;

    the_gui_in_use = TRUE;
    return gui;
}

void gui_destroy(struct AmiDropGui *gui)
{
    if (!gui) return;
    gui_shutdown(gui);
    the_gui_in_use = FALSE;
}

ULONG gui_signal_mask(const struct AmiDropGui *gui)
{
    return gui ? gui->signal_mask : 0UL;
}

void gui_signals_received(struct AmiDropGui *gui, ULONG signals)
{
    if (gui) gui->pending_signals = signals;
}

BOOL gui_next_event(struct AmiDropGui *gui, struct AmiDropGuiEvent *event)
{
    ULONG id;

    if (!event) return FALSE;
    event->type = GUI_EVENT_NONE;
    event->value = 0;
    if (!gui || !gui->app) return FALSE;

    /* A quit held over from the Preferences dialog comes first - see
       the_pending_quit. */
    if (the_pending_quit) {
        the_pending_quit = FALSE;
        event->type = GUI_EVENT_QUIT;
        return TRUE;
    }

    for (;;) {
        ULONG signals = gui->pending_signals;

        /* NewInput consumes the signals it was given and reports back the
           set to wait for next time. */
        id = DoMethod(gui->app, MUIM_Application_NewInput, &signals);
        gui->pending_signals = 0;
        gui->signal_mask = signals;

        if (id == (ULONG)MUIV_Application_ReturnID_Quit || id == RID_QUIT) {
            event->type = GUI_EVENT_QUIT;
            return TRUE;
        }
        if (id == RID_ABORT) {
            event->type = GUI_EVENT_ABORT;
            return TRUE;
        }
        if (id == RID_CLEAR) {
            event->type = GUI_EVENT_CLEAR;
            return TRUE;
        }
        if (id == RID_COPY) {
            /* Handled entirely inside the frontend - main.c has no business
               knowing about a clipboard, and the contract stays as it is.
               Silent when it works. */
            if (!amidrop_clip_put_text(gui->copy_url)) {
                if (gui->uploading) {
                    /* No requester while receiving: it is modal, so the main
                       loop stops - no recv(), and no server_check_timeout().
                       Left standing for the 30 seconds of
                       AMIDROP_IDLE_TIMEOUT it would make AmiDrop delete the
                       file it is in the middle of receiving.  main.c refuses
                       to open Preferences during a transfer for the same
                       reason.  Silence would be wrong too: FALSE here can
                       mean the write started and CMD_UPDATE failed, leaving
                       the OLD clip current, so the user would paste
                       something else and never be told.  A beep costs no
                       window, no allocation and no modality.

                       Not the whole risk, and worth being honest about: the
                       DoIO() above can block on its own, because
                       clipboard.doc warns that "while an application is in
                       the middle of reading a clip, any attempts to write
                       new data to the clipboard are held off".  Refusing the
                       click outright during a transfer would close that too,
                       at the cost of a button that mysteriously does nothing;
                       the risk is accepted instead.

                       The window's own screen, not NULL: intuition.doc says a
                       NULL screen beeps every screen in the display and
                       "should be reserved for dire circumstances".  A closed
                       window reports NULL for it
                       (MUI_Window.doc/MUIA_Window_Screen), which lands back
                       on that behaviour and is the best available then. */
                    ULONG screen = 0;
                    get(gui->window, MUIA_Window_Screen, &screen);
                    DisplayBeep((struct Screen *)screen);
                } else {
                    /* Asleep meanwhile, so the buttons and menu behind it
                       cannot push ids onto the queue this loop is reading -
                       the precaution gui_preferences() takes.  Both calls are
                       gated on the window actually being open: MUI_Window.doc
                       says "Only opened windows can be put to sleep. However,
                       currently closed windows can be taken out of sleep
                       nevertheless", and MUI closes every window behind our
                       back when the user iconifies (MUIA_Application_-
                       Iconified: "There is no way for you to prevent your
                       application from being iconified").  Ungated, an
                       iconify racing this id would drop the sleep, apply the
                       wake, and leave the nesting count one short. */
                    ULONG open = FALSE;
                    get(gui->window, MUIA_Window_Open, &open);
                    if (open) set(gui->window, MUIA_Window_Sleep, TRUE);
                    gui_message(gui, "AmiDrop",
                                "The link could not be put on the clipboard.");
                    if (open) set(gui->window, MUIA_Window_Sleep, FALSE);
                }
            }
            continue;
        }
        if (id >= RID_MENU_BASE + MID_START_SERVER &&
            id <= RID_MENU_BASE + MID_ABOUT) {
            event->type = GUI_EVENT_MENU;
            event->value = id - RID_MENU_BASE;
            return TRUE;
        }
        if (id == 0) return FALSE;
        /* Any other ID is not ours - keep draining. */
    }
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */

void gui_force_qr_redraw(struct AmiDropGui *gui)
{
    if (gui && gui->qr_object) DoMethod(gui->qr_object, MUIM_AmiDropQR_Changed);
}

static void update_qr(struct AmiDropGui *gui, const struct AmiDropServer *server)
{
    char payload[sizeof(gui->qr_payload)];
    BOOL valid = FALSE;

    /* server->address holds the literal "http://<Amiga-IP>:8080/" template
       until an IPv4 address is found; encoding that produces a scannable code
       for a URL that cannot work. */
    if (!server || !server->address_valid ||
        !server->address[0] || !server->session_token[0]) {
        payload[0] = '\0';
    } else if (gui->qr_valid &&
               amidrop_qr_payload_current(gui->qr_payload, server->address,
                                          server->session_token)) {
        /* Nothing changed, nothing to rebuild - and this is on the path taken
           for every received network block.  The TOKEN is part of the test:
           a server stop/start regenerates it while the address stays byte
           identical, and comparing only the address once left a stale symbol
           that locked the sender out after five scans. */
        return;
    } else {
        /* Same composer as the clipboard path, so the symbol and the
           copied link cannot say different things. */
        amidrop_compose_url(payload, sizeof(payload), server->address,
                            server->session_token);
        if (strlen(payload) > AMIDROP_QR_MAX_PAYLOAD) payload[0] = '\0';
    }

    /* Nothing to do when the state has not changed.  Testing only the
       valid case left the invalid one re-issuing a redraw and a MUIA_ShowMe
       set on every single gui_redraw() while no address was known, and
       MUI_Area.doc warns that showing or hiding forces a window refresh. */
    if (payload[0]) {
        if (gui->qr_valid && strcmp(gui->qr_payload, payload) == 0) return;
    } else {
        if (!gui->qr_valid && !gui->qr_payload[0]) return;
    }

    if (payload[0]) {
        memset(gui->qr_modules, 0, sizeof(gui->qr_modules));
        if (qrcode_initText(&gui->qr, gui->qr_modules, AMIDROP_QR_VERSION,
                            ECC_LOW, payload) == 0) {
            strncpy(gui->qr_payload, payload, sizeof(gui->qr_payload) - 1);
            gui->qr_payload[sizeof(gui->qr_payload) - 1] = '\0';
            valid = TRUE;
        }
    }

    if (!valid) gui->qr_payload[0] = '\0';
    gui->qr_valid = valid;
    if (gui->qr_object) {
        /* The symbol itself may have changed while staying valid, and that
           is invisible to an attribute compare - so always ask for a repaint
           once we get here, which only happens when something did change. */
        set(gui->qr_object, MUIA_AmiDropQR_Valid, valid);
        DoMethod(gui->qr_object, MUIM_AmiDropQR_Changed);
    }
    /* "Scan to send" under an empty white square is worse than no label. */
    if (gui->qr_label) set(gui->qr_label, MUIA_ShowMe, valid);
}

void gui_redraw(struct AmiDropGui *gui, const struct AmiDropServer *server,
                const struct AmiDropPrefs *prefs)
{
    char line[192];
    LONG percent = 0;

    if (!gui || !gui->app || !server || !prefs) return;

    update_qr(gui, server);

    set_text(gui->address_text, gui->shown_address, sizeof(gui->shown_address),
             server->address[0] ? server->address : "Server not running");

    set_text(gui->code_text, gui->shown_code, sizeof(gui->shown_code),
             server->access_code[0] ? server->access_code : "------");

    set_text(gui->receive_text, gui->shown_receive, sizeof(gui->shown_receive),
             prefs->receive_dir);

    set_text(gui->status_text, gui->shown_status, sizeof(gui->shown_status),
             server->status);

    /* The clipboard carries what the QR symbol carries, so a sender can be
       given the link either way.  Only the empty/non-empty transition touches
       the button, so this does not issue a SetAttrs on every received network
       block. */
    {
        char url[sizeof(gui->copy_url)];
        amidrop_compose_url(url, sizeof(url),
                            server->address_valid ? server->address : "",
                            server->session_token);
        if (strcmp(url, gui->copy_url) != 0) {
            BOOL had = gui->copy_url[0] ? TRUE : FALSE;
            BOOL has = url[0] ? TRUE : FALSE;
            strcpy(gui->copy_url, url);
            if (had != has)
                set(gui->copy_button, MUIA_Disabled, has ? FALSE : TRUE);
        }
    }

    /* Depends only on a preference, so rebuilding it on every received
       network block would run the printf engine for nothing. */
    if (prefs->max_file_kb != gui->shown_limit_kb) {
        gui->shown_limit_kb = prefs->max_file_kb;
        format_limit_text(line, sizeof(line), prefs->max_file_kb);
        set_text(gui->limit_text, gui->shown_limit, sizeof(gui->shown_limit), line);
    }

    if (server->uploading && server->upload_total > 0) {
        percent = (LONG)amidrop_percent(server->upload_received, server->upload_total);
    } else if (strncmp(server->status, "Received:", 9) == 0) {
        percent = 100;
    }

    /* The info text never changes: it is the "%ld %%" format set at object
       creation, and Gauge substitutes MUIA_Gauge_Current into it by itself.
       Passing a fresh string here would hand Gauge a pointer it keeps but we
       do not - a stack buffer did exactly that once and the gauge rendered
       freed memory as garbage text. */
    if (percent != gui->shown_percent) {
        gui->shown_percent = percent;
        set(gui->gauge, MUIA_Gauge_Current, percent);
    }
}

void gui_sync_history(struct AmiDropGui *gui, const struct AmiDropServer *server)
{
    UWORD i;

    if (!gui || !gui->list || !server) return;
    /* Hidden section: the GadTools frontend has no list gadget at all in this
       state, so it does no work here either. */
    if (!gui->show_transfer_information) return;
    if (gui->history_generation == server->transfer_generation) return;
    gui->history_generation = server->transfer_generation;

    set(gui->list, MUIA_List_Quiet, TRUE);
    DoMethod(gui->list, MUIM_List_Clear);
    /* add_transfer_history() shifts the array up and writes the new entry to
       transfers[0], so index 0 is the NEWEST.  Insert in array order, which
       is what the list title promises and what the other two frontends do. */
    for (i = 0; i < server->transfer_count &&
                i < AMIDROP_TRANSFER_HISTORY; ++i) {
        DoMethod(gui->list, MUIM_List_InsertSingle,
                 (ULONG)server->transfers[i].display, MUIV_List_Insert_Bottom);
    }
    set(gui->list, MUIA_List_Quiet, FALSE);
}

void gui_set_abort_enabled(struct AmiDropGui *gui, BOOL enabled)
{
    if (!gui) return;
    gui->uploading = enabled;
    if (gui->abort_button)
        set(gui->abort_button, MUIA_Disabled, enabled ? FALSE : TRUE);
}

/* ------------------------------------------------------------------ */
/* Requesters                                                          */
/* ------------------------------------------------------------------ */

/* Neither Intuition nor MUI word-wraps a requester body - both clip - and
   this program's diagnostics run to 200 characters.  Single-threaded, so one
   static buffer is enough; the GadTools frontend does the same. */
static const char *wrapped(const char *text)
{
    static char buffer[512];
    return amidrop_wrap_text(text, buffer, sizeof(buffer), 52);
}

static void easy_fallback(const char *title, const char *text, const char *gadgets)
{
    struct EasyStruct easy;
    const char *body = wrapped(text);

    memset(&easy, 0, sizeof(easy));
    easy.es_StructSize = sizeof(easy);
    easy.es_Title = (STRPTR)(title ? title : AMIDROP_NAME);
    /* es_TextFormat is a RawDoFmt format string: passing the message itself
       would make a '%' in it read arguments from a NULL list. */
    easy.es_TextFormat = (STRPTR)"%s";
    easy.es_GadgetFormat = (STRPTR)gadgets;
    EasyRequestArgs(NULL, &easy, NULL, (APTR)&body);
}

void gui_message(struct AmiDropGui *gui, const char *title, const char *text)
{
    if (!gui || !gui->app) {
        /* Startup and shutdown errors happen with no application object. */
        easy_fallback(title, text, "OK");
        return;
    }
    MUI_Request(gui->app, gui->window, 0,
                (char *)(title ? title : AMIDROP_NAME),
                (char *)"*OK", (char *)"%s", (ULONG)wrapped(text));
}

BOOL gui_confirm(struct AmiDropGui *gui, const char *title, const char *text,
                 const char *yes_text, const char *no_text)
{
    char gadgets[96];

    if (!yes_text) yes_text = "Yes";
    if (!no_text) no_text = "No";
    /* MUI marks the default (return key) gadget with a leading asterisk; the
       cancel choice must be the rightmost one, which is gadget 0. */
    snprintf(gadgets, sizeof(gadgets), "%s|*%s", yes_text, no_text);

    if (!gui || !gui->app) {
        /* gui.h promises a NULL interface still asks the user; answering
           "no" on their behalf would silently cancel a quit.  EasyRequest
           has no '*' default-gadget convention, so it gets its own plain
           string, and the body goes through a format like everywhere else. */
        struct EasyStruct easy;
        char plain[96];
        const char *body = wrapped(text);

        snprintf(plain, sizeof(plain), "%s|%s", yes_text, no_text);
        memset(&easy, 0, sizeof(easy));
        easy.es_StructSize = sizeof(easy);
        easy.es_Title = (STRPTR)(title ? title : AMIDROP_NAME);
        easy.es_TextFormat = (STRPTR)"%s";
        easy.es_GadgetFormat = (STRPTR)plain;
        return EasyRequestArgs(NULL, &easy, NULL, (APTR)&body) == 1;
    }

    return MUI_Request(gui->app, gui->window, 0,
                       (char *)(title ? title : AMIDROP_NAME),
                       gadgets, (char *)"%s", (ULONG)wrapped(text)) == 1;
}

const char *gui_startup_hint(void)
{
    return "This build needs MUI 3.8 (muimaster.library 19) or newer.";
}

void gui_show_about(struct AmiDropGui *gui)
{
    if (!gui || !gui->app) return;
    MUI_Request(gui->app, gui->window, 0, (char *)"About AmiDrop",
                (char *)"*OK", (char *)"%s",
                (ULONG)(const char *)
                /* This is the MUI binary, so it must name itself - not the
                   GadTools program it was built from. */
                AMIDROP_MUI_NAME " " AMIDROP_VERSION " (" AMIDROP_MUI_DATE ")\n\n"
                "Receives files from a phone, tablet or PC\n"
                "over the local network - the sender only\n"
                "needs a web browser.\n\n"
                "Original program by Andiweli.\n"
                "MUI edition by Jan Zahurancik / AmiKit.\n"
                "Icon by Martin 'Mason' Merz.\n"
                "DualPNG icons by Ken E. Lester & Jan.\n"
                "QR code generation by Richard Moore\n"
                "and Project Nayuki.\n\n"
                "MIT licensed.");
}

/* ------------------------------------------------------------------ */
/* Preferences                                                         */
/* ------------------------------------------------------------------ */

/* Popasl runs its requester in a task of its own, and MUI_Popasl.doc is
   explicit: disposing the object while that requester is open freezes our
   task.  Refusing to leave the dialog while it is up keeps every later path -
   closing the window, quitting, Ctrl-C - out of that state. */
static BOOL requester_is_open(struct AmiDropGui *gui)
{
    ULONG active = 0;

    if (!gui || !gui->prefs_pop) return FALSE;
    get(gui->prefs_pop, MUIA_Popasl_Active, &active);
    if (!active) return FALSE;

    gui_message(gui, "AmiDrop Preferences",
                "Close the folder requester first.");
    return TRUE;
}

int gui_preferences(struct AmiDropGui *gui, struct AmiDropPrefs *prefs)
{
    ULONG signals = 0;
    ULONG open = FALSE;
    int action = PREFS_ACTION_CANCEL;
    BOOL ctrl_c = FALSE;
    BOOL quit_requested = FALSE;
    char port_buffer[8];

    if (!gui || !gui->app || !gui->prefs_window || !prefs) return PREFS_ACTION_CANCEL;

    snprintf(port_buffer, sizeof(port_buffer), "%lu", (unsigned long)prefs->port);
    SetAttrs(gui->prefs_dir, MUIA_String_Contents, (ULONG)prefs->receive_dir, TAG_DONE);
    SetAttrs(gui->prefs_port, MUIA_String_Contents, (ULONG)port_buffer, TAG_DONE);
    SetAttrs(gui->prefs_size, MUIA_Cycle_Active, max_size_index(prefs->max_file_kb), TAG_DONE);
    SetAttrs(gui->prefs_start, MUIA_Selected, prefs->start_server ? TRUE : FALSE, TAG_DONE);
    SetAttrs(gui->prefs_ignore, MUIA_Selected, prefs->ignore_free_space ? TRUE : FALSE, TAG_DONE);
    SetAttrs(gui->prefs_show_info, MUIA_Selected, prefs->show_transfer_information ? TRUE : FALSE, TAG_DONE);

    set(gui->prefs_window, MUIA_Window_Open, TRUE);
    get(gui->prefs_window, MUIA_Window_Open, &open);
    if (!open) return PREFS_ACTION_CANCEL;

    /* Asleep, the main window produces no return IDs at all.  Without this
       its buttons and menu would push IDs onto the same application-wide
       queue this loop reads, and the loop would swallow them. */
    set(gui->window, MUIA_Window_Sleep, TRUE);

    /* A nested input loop, like the GadTools frontend.  The main window is
       asleep for its duration (see above), and the server loop is paused;
       main.c refuses to open this dialog during a transfer, so no upload can
       stall here. */
    for (;;) {
        ULONG id = DoMethod(gui->app, MUIM_Application_NewInput, &signals);

        if (id == PREFS_RID_SAVE || id == PREFS_RID_USE) {
            ULONG value = 0;
            char *text = NULL;

            if (requester_is_open(gui)) { signals = 0; continue; }

            get(gui->prefs_port, MUIA_String_Integer, &value);
            if (!prefs_valid_port(value)) {
                gui_message(gui, "AmiDrop Preferences",
                            "The port number is out of range. Nothing was changed.");
                /* Every path back into the loop must clear the mask, or the
                   next NewInput is told those signals have just arrived. */
                signals = 0;
                continue;
            }

            /* An emptied field must not silently keep the old folder and then
               report "Preferences saved" - the other two frontends refuse. */
            get(gui->prefs_dir, MUIA_String_Contents, &text);
            if (!text || !*text) {
                gui_message(gui, "AmiDrop Preferences",
                            "Please choose a receive folder.");
                signals = 0;
                continue;
            }

            prefs->port = (UWORD)value;
            strncpy(prefs->receive_dir, text, sizeof(prefs->receive_dir) - 1);
            prefs->receive_dir[sizeof(prefs->receive_dir) - 1] = '\0';

            get(gui->prefs_size, MUIA_Cycle_Active, &value);
            if (value < 10) prefs->max_file_kb = size_values_kb[value];

            get(gui->prefs_start, MUIA_Selected, &value);
            prefs->start_server = value ? TRUE : FALSE;
            get(gui->prefs_ignore, MUIA_Selected, &value);
            prefs->ignore_free_space = value ? TRUE : FALSE;
            get(gui->prefs_show_info, MUIA_Selected, &value);
            prefs->show_transfer_information = value ? TRUE : FALSE;

            action = (id == PREFS_RID_SAVE) ? PREFS_ACTION_SAVE : PREFS_ACTION_USE;
            break;
        }
        if (id == PREFS_RID_CANCEL ||
            id == (ULONG)MUIV_Application_ReturnID_Quit || id == RID_QUIT) {
            if (requester_is_open(gui)) { signals = 0; continue; }
            /* A quit from Exchange or ARexx must not be downgraded to
               "dialog cancelled" - carry it out to the main loop. */
            if (id != PREFS_RID_CANCEL) quit_requested = TRUE;
            action = PREFS_ACTION_CANCEL;
            break;
        }
        if (signals) {
            signals = Wait(signals | SIGBREAKF_CTRL_C);
            if (signals & SIGBREAKF_CTRL_C) {
                /* Remember it even when the dialog cannot close yet: the
                   Wait() above has already consumed the signal, so dropping
                   it here would lose the break request entirely. */
                ctrl_c = TRUE;
                if (requester_is_open(gui)) { signals = 0; continue; }
                action = PREFS_ACTION_CANCEL;
                break;
            }
        }
    }

    set(gui->prefs_window, MUIA_Window_Open, FALSE);
    set(gui->window, MUIA_Window_Sleep, FALSE);

    /* This loop consumed whatever the main loop was waiting for, so hand it
       a fresh mask.  Pass a zeroed in-value: handing NewInput the previous
       wait mask would claim every one of those signals had just arrived.
       Drain rather than pop once, so a queued id is not silently discarded.
       Nothing is put back INSIDE this loop: MUI's return ids are one fifo,
       so re-posting an id here would make NewInput hand it straight back and
       the drain would never end.  Only a quit has to survive, and the main
       window was asleep throughout, so no menu or button id can be waiting. */
    {
        ULONG fresh = 0;
        ULONG leftover;

        while ((leftover = DoMethod(gui->app, MUIM_Application_NewInput,
                                    &fresh)) != 0) {
            if (leftover == (ULONG)MUIV_Application_ReturnID_Quit ||
                leftover == RID_QUIT)
                quit_requested = TRUE;
            /* NewInput wants the result of Wait() (MUI_Application.doc); it
               has just written the NEXT wait mask through this pointer, so
               handing that back would claim all those signals had arrived.
               The terminating call does not run this body, so `fresh` still
               holds the mask we want below. */
            fresh = 0;
        }
        gui->signal_mask = fresh;
    }
    gui->pending_signals = 0;

    /* Remembered rather than put back on the application's fifo: applying a
       layout change disposes that application, and the id would go with it.
       gui_next_event() hands it over on the next pass, from whichever
       interface is current by then. */
    if (quit_requested) the_pending_quit = TRUE;

    /* Ctrl-C was consumed by the Wait() above; main.c is the only place that
       turns it into a quit, so put it back. */
    if (ctrl_c) Signal(FindTask(NULL), SIGBREAKF_CTRL_C);
    return action;
}
