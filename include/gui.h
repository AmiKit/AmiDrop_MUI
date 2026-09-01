#ifndef AMIDROP_GUI_H
#define AMIDROP_GUI_H

#include <exec/types.h>

#include "amidrop.h"

struct AmiDropServer;
struct AmiDropPrefs;

/* The GUI instance is opaque.  Every frontend (GadTools, ReAction, MUI)
   defines its own struct AmiDropGui in its own source file, so main.c stays
   free of any toolkit detail and one main loop serves all of them. */
struct AmiDropGui;

/* Menu actions.  A frontend translates whatever its own menu system reports
   into one of these and hands it over as GUI_EVENT_MENU. */
#define MID_START_SERVER  1UL
#define MID_STOP_SERVER   2UL
#define MID_PREFS         3UL
#define MID_QUIT          4UL
#define MID_ABOUT         5UL

#define PREFS_ACTION_CANCEL 0
#define PREFS_ACTION_USE    1
#define PREFS_ACTION_SAVE   2

/* Frontend independent events.  Anything toolkit specific - refreshing a
   simple-refresh window, walking a chain of multi-selected menu items - is
   handled inside the frontend and never reaches the main loop. */
#define GUI_EVENT_NONE    0
#define GUI_EVENT_QUIT    1
#define GUI_EVENT_ABORT   2
#define GUI_EVENT_CLEAR   3
#define GUI_EVENT_MENU    4  /* value holds one of the MID_ codes */
#define GUI_EVENT_REFRESH 5

struct AmiDropGuiEvent {
    UWORD type;
    ULONG value;
};

/* One line naming what this frontend needs, for the startup error message.
   Valid before gui_create() and after gui_destroy(). */
const char *gui_startup_hint(void);

/* Creates and opens the interface.  Returns NULL on failure; the pointer
   stays valid until gui_destroy().  The preferences are needed at this point
   because they can decide the window layout. */
struct AmiDropGui *gui_create(const struct AmiDropPrefs *prefs);
void gui_destroy(struct AmiDropGui *gui);

/* Every function below accepts a NULL interface.  A window rebuild that fails
   leaves the caller holding NULL until it manages to open one again, and the
   main loop keeps running meanwhile.  gui_message() and gui_confirm() still
   put their question to the user in that state; the rest do nothing and
   return a neutral value. */

/* Signals the main loop has to wait for on behalf of the interface. */
ULONG gui_signal_mask(const struct AmiDropGui *gui);

/* Handed the signals the main loop actually received, before the events are
   pulled.  Frontends built on Intuition message ports ignore this; MUI needs
   it, because MUIM_Application_NewInput wants to be told what woke us. */
void gui_signals_received(struct AmiDropGui *gui, ULONG signals);

/* Pulls one pending event.  Returns FALSE once the queue is empty. */
BOOL gui_next_event(struct AmiDropGui *gui, struct AmiDropGuiEvent *event);

void gui_redraw(struct AmiDropGui *gui, const struct AmiDropServer *server,
                const struct AmiDropPrefs *prefs);
void gui_sync_history(struct AmiDropGui *gui, const struct AmiDropServer *server);
void gui_force_qr_redraw(struct AmiDropGui *gui);
/* Enables or disables the Abort control.  The caller passes whether a
   transfer is running, and a frontend may treat it as exactly that: the MUI
   one remembers it, because a modal requester raised mid-transfer stops the
   main loop long enough for the idle timeout to discard the file.  Do not
   call it for any other reason. */
void gui_set_abort_enabled(struct AmiDropGui *gui, BOOL enabled);
int gui_preferences(struct AmiDropGui *gui, struct AmiDropPrefs *prefs);
void gui_show_about(struct AmiDropGui *gui);

/* These two ask the user even with no interface, so that startup errors can
   be reported before, or after, one exists. */
void gui_message(struct AmiDropGui *gui, const char *title, const char *text);
BOOL gui_confirm(struct AmiDropGui *gui, const char *title, const char *text,
                 const char *yes_text, const char *no_text);

#endif
