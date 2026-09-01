#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amidrop.h"
#include "prefs.h"
#include "server.h"
#include "gui.h"
#include "util.h"

/* Only the libraries every frontend needs are opened here.  Toolkit specific
   ones (gadtools/asl, the ReAction classes, muimaster) belong to the frontend
   and are opened by gui_create(). */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *IconBase = NULL;
struct Library *SocketBase = NULL;

static struct MsgPort *instance_port = NULL;

#if defined(AMIDROP_GUI_MUI)
#define AMIDROP_VER_NAME AMIDROP_MUI_NAME
#define AMIDROP_VER_DATE AMIDROP_MUI_DATE
#elif defined(AMIDROP_GUI_REACTION)
#define AMIDROP_VER_NAME AMIDROP_REACTION_NAME
#define AMIDROP_VER_DATE AMIDROP_REACTION_DATE
#else
#define AMIDROP_VER_NAME AMIDROP_NAME
#define AMIDROP_VER_DATE AMIDROP_DATE
#endif

static const char version_string[] __attribute__((used)) =
    "$VER: " AMIDROP_VER_NAME " " AMIDROP_VERSION " (" AMIDROP_VER_DATE ")";

static BOOL open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (!IntuitionBase) return FALSE;
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    IconBase = OpenLibrary((STRPTR)"icon.library", 39);
    SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);

    return GfxBase && IconBase && SocketBase;
}

static void close_libraries(void)
{
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
    if (IconBase) { CloseLibrary(IconBase); IconBase = NULL; }
    if (GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase = NULL; }
    if (IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase = NULL; }
}

static BOOL claim_single_instance(void)
{
    struct MsgPort *port;

    port = CreateMsgPort();
    if (!port) return FALSE;
    port->mp_Node.ln_Name = (char *)AMIDROP_INSTANCE_PORT;

    Forbid();
    if (FindPort((STRPTR)AMIDROP_INSTANCE_PORT)) {
        Permit();
        DeleteMsgPort(port);
        return FALSE;
    }
    AddPort(port);
    Permit();
    instance_port = port;
    return TRUE;
}

static void release_single_instance(void)
{
    if (!instance_port) return;
    Forbid();
    RemPort(instance_port);
    Permit();
    DeleteMsgPort(instance_port);
    instance_port = NULL;
}

static BOOL parse_ulong_exact(const char *text, ULONG *value)
{
    char *end;
    unsigned long number;
    if (!text || !*text || !value) return FALSE;
    number = strtoul(text, &end, 10);
    if (end == text || *end != '\0') return FALSE;
    *value = (ULONG)number;
    return TRUE;
}

static BOOL apply_named_option(struct AmiDropPrefs *prefs, const char *name, const char *value)
{
    ULONG number;
    BOOL boolean_value;

    if (!prefs || !name || !value) return FALSE;
    if (amidrop_ascii_casecmp_n(name, "PORT", 5) == 0 && name[4] == '\0') {
        if (!parse_ulong_exact(value, &number) || !prefs_valid_port(number)) return FALSE;
        prefs->port = (UWORD)number;
        return TRUE;
    }
    if (amidrop_ascii_casecmp_n(name, "MAXSIZE", 8) == 0 && name[7] == '\0') {
        /* Backward-compatible MAXSIZE value in whole megabytes. */
        if (!parse_ulong_exact(value, &number) || !prefs_valid_max_file_mb(number)) return FALSE;
        prefs->max_file_kb = number * 1024UL;
        return TRUE;
    }
    if (amidrop_ascii_casecmp_n(name, "MAXSIZEKB", 10) == 0 && name[9] == '\0') {
        if (!parse_ulong_exact(value, &number) || !prefs_valid_max_file_kb(number)) return FALSE;
        prefs->max_file_kb = number;
        return TRUE;
    }
    if (amidrop_ascii_casecmp_n(name, "RECEIVEDIR", 11) == 0 && name[10] == '\0') {
        if (!*value) return FALSE;
        strncpy(prefs->receive_dir, value, sizeof(prefs->receive_dir) - 1);
        prefs->receive_dir[sizeof(prefs->receive_dir) - 1] = '\0';
        return TRUE;
    }
    if (amidrop_ascii_casecmp_n(name, "STARTSERVER", 12) == 0 && name[11] == '\0') {
        if (!prefs_parse_yes_no(value, &boolean_value)) return FALSE;
        prefs->start_server = boolean_value;
        return TRUE;
    }
    if (amidrop_ascii_casecmp_n(name, "IGNORESPACE", 12) == 0 && name[11] == '\0') {
        if (!prefs_parse_yes_no(value, &boolean_value)) return FALSE;
        prefs->ignore_free_space = boolean_value;
        return TRUE;
    }
    if (amidrop_ascii_casecmp_n(name, "SHOWTRANSFERINFO", 17) == 0 && name[16] == '\0') {
        if (!prefs_parse_yes_no(value, &boolean_value)) return FALSE;
        prefs->show_transfer_information = boolean_value;
        return TRUE;
    }
    return FALSE;
}

static BOOL apply_assignment(struct AmiDropPrefs *prefs, const char *assignment)
{
    char name[32];
    const char *eq;
    size_t len;

    if (!prefs || !assignment) return FALSE;
    eq = strchr(assignment, '=');
    if (!eq) return FALSE;
    len = (size_t)(eq - assignment);
    if (len == 0 || len >= sizeof(name)) return FALSE;
    memcpy(name, assignment, len);
    name[len] = '\0';
    return apply_named_option(prefs, name, eq + 1);
}

static const char *find_tooltype_value(STRPTR *tooltypes, const char *key)
{
    size_t key_len;

    if (!tooltypes || !key || !*key) return NULL;
    key_len = strlen(key);

    while (*tooltypes) {
        const char *entry = (const char *)(APTR)*tooltypes;

        /* IconEdit surrounds disabled ToolTypes with parentheses. */
        if (entry && entry[0] != '(' &&
            amidrop_ascii_casecmp_n(entry, key, key_len) == 0 &&
            entry[key_len] == '=') {
            return entry + key_len + 1;
        }
        ++tooltypes;
    }
    return NULL;
}

static BOOL apply_tooltypes(struct AmiDropPrefs *prefs, STRPTR *tooltypes)
{
    static const char *keys[] = { "PORT", "MAXSIZE", "MAXSIZEKB", "RECEIVEDIR", "STARTSERVER", "IGNORESPACE", "SHOWTRANSFERINFO" };
    UWORD i;
    BOOL all_valid = TRUE;

    if (!prefs || !tooltypes) return TRUE;
    for (i = 0; i < 7; ++i) {
        const char *value = find_tooltype_value(tooltypes, keys[i]);
        if (value && !apply_named_option(prefs, keys[i], value)) all_valid = FALSE;
    }
    return all_valid;
}

static BOOL apply_launch_options(int argc, char **argv, struct AmiDropPrefs *prefs)
{
    BOOL all_valid = TRUE;
    int i;

    if (argc > 0) {
        for (i = 1; i < argc; ++i) {
            if (strchr(argv[i], '=') && !apply_assignment(prefs, argv[i])) all_valid = FALSE;
        }
    } else if (argv && IconBase) {
        struct WBStartup *startup = (struct WBStartup *)argv;
        if (startup->sm_NumArgs > 0 && startup->sm_ArgList) {
            struct WBArg *arg = &startup->sm_ArgList[0];
            BPTR old_dir = CurrentDir(arg->wa_Lock);
            struct DiskObject *disk_object = GetDiskObject(arg->wa_Name);
            CurrentDir(old_dir);
            if (disk_object) {
                if (!apply_tooltypes(prefs, disk_object->do_ToolTypes)) all_valid = FALSE;
                FreeDiskObject(disk_object);
            }
        }
    }
    return all_valid;
}

/* Its own noinline function: this buffer would otherwise sit in main()'s
   frame for the whole session even though it is used once, on a path that
   ends the program.  Name the missing prerequisite - merging the three
   frontends into one main() would otherwise have thrown away the
   toolkit-specific hint each of them used to print. */
static void report_startup_failure(void) __attribute__((noinline));
static void report_startup_failure(void)
{
    char failure[256];

    snprintf(failure, sizeof(failure),
             "Could not open the user interface.\n\n%s", gui_startup_hint());
    gui_message(NULL, "AmiDrop startup error", failure);
}

static void copy_status(struct AmiDropServer *server, const char *text)
{
    if (!server) return;
    strncpy(server->status, text ? text : "", sizeof(server->status) - 1);
    server->status[sizeof(server->status) - 1] = '\0';
    server->dirty = TRUE;
}

static void present_server_alert(struct AmiDropGui *gui, struct AmiDropServer *server,
                                 ULONG *last_generation)
{
    if (!server || !last_generation) return;
    if (server->alert_generation == *last_generation) return;
    *last_generation = server->alert_generation;
    if (server->alert[0]) gui_message(gui, "AmiDrop", server->alert);
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

/* The GadTools dialog asks this question inside itself.  The MUI and ReAction
   dialogs did not ask it at all, so a limit the volume cannot hold was
   accepted in silence.  Asking here covers all three frontends with one
   implementation, and it cannot double-ask: when the GadTools dialog already
   asked and the user answered Ignore, ignore_free_space is TRUE and this
   returns immediately. */
/* noinline on the three big helpers below.  GCC inlines them into main(),
   and main()'s frame then lives for the whole program: measured 812 bytes
   against upstream's 56.  A 68k task stack is small, so their locals must
   exist only while they run.  Cost: one jsr per menu action. */
static BOOL confirm_capacity(struct AmiDropGui *gui, struct AmiDropPrefs *candidate)
    __attribute__((noinline));
static BOOL confirm_capacity(struct AmiDropGui *gui, struct AmiDropPrefs *candidate)
{
    BOOL known = FALSE;
    unsigned long long free_bytes;
    unsigned long long required;
    char free_text[32];
    char need_text[32];
    char body[384];

    if (!candidate || candidate->ignore_free_space) return TRUE;

    free_bytes = server_free_bytes_for_path(candidate->receive_dir, &known);
    if (!known) return TRUE;

    required = server_required_free_bytes((unsigned long long)candidate->max_file_kb * 1024ULL);
    if (free_bytes >= required) return TRUE;

    format_capacity(free_text, sizeof(free_text), free_bytes);
    format_capacity(need_text, sizeof(need_text), required);
    snprintf(body, sizeof(body),
             /* Same wording as the GadTools dialog asks (gui.c), so the two
                do not drift apart. */
             "The selected volume has %s free.\nThe selected file-size limit can require about %s including a safety reserve.\n\nIgnore the free-space check for this receive folder?",
             free_text, need_text);

    if (gui_confirm(gui, "AmiDrop - low free space", body, "Ignore", "Cancel")) {
        candidate->ignore_free_space = TRUE;
        return TRUE;
    }
    return FALSE;
}

/* ShowTransferInformation changes the window layout, and the GadTools
   frontend builds its layout when the window opens - so changing it means
   building the interface again.  Upstream does this with gui_close/gui_open;
   here it goes through the contract, which hands back a new pointer, so the
   caller's handle has to be updated.  MUI would not need any of this - it can
   hide and show objects in a window that is already open - but rebuilding is
   the one route the contract offers all three frontends, and a path taken at
   most a few times in a session does not deserve one of its own.  ReAction
   ignores the preference and simply comes back identical. */
static BOOL reopen_main_gui(struct AmiDropGui **gui, const struct AmiDropServer *server,
                            const struct AmiDropPrefs *prefs)
{
    struct AmiDropGui *rebuilt;

    if (!gui || !server || !prefs) return FALSE;

    /* *gui may legitimately be NULL here: a failed rebuild leaves it so, and
       the caller then calls us again to restore the previous layout.  Demanding
       a live handle made that retry return immediately, which turned upstream's
       fallback into dead code and left the program running with no window. */
    if (*gui) {
        gui_destroy(*gui);
        *gui = NULL;
    }

    rebuilt = gui_create(prefs);
    if (!rebuilt) return FALSE;

    *gui = rebuilt;
    gui_sync_history(rebuilt, server);
    gui_set_abort_enabled(rebuilt, server_is_uploading(server));
    gui_redraw(rebuilt, server, prefs);
    return TRUE;
}

static BOOL apply_preferences(struct AmiDropGui **gui, struct AmiDropServer *server,
                              struct AmiDropPrefs *prefs,
                              const struct AmiDropPrefs *candidate, BOOL save)
    __attribute__((noinline));
static BOOL apply_preferences(struct AmiDropGui **gui, struct AmiDropServer *server,
                              struct AmiDropPrefs *prefs,
                              const struct AmiDropPrefs *candidate, BOOL save)
{
    struct AmiDropPrefs old_prefs;
    /* Local again: with this function kept out of main() its frame exists
       only while it runs, so the copy costs nothing for the rest of the
       session and 268 bytes of BSS are given back. */
    struct AmiDropPrefs accepted;
    BOOL was_running;
    BOOL port_changed;
    BOOL layout_changed;

    if (!gui || !*gui || !server || !prefs || !candidate) return FALSE;

    /* confirm_capacity() may turn on ignore_free_space, so it needs a
       writable copy; everything below works from that copy. */
    accepted = *candidate;
    if (!confirm_capacity(*gui, &accepted)) return FALSE;
    candidate = &accepted;

    old_prefs = *prefs;
    was_running = server->running;
    port_changed = old_prefs.port != candidate->port;
    layout_changed = old_prefs.show_transfer_information != candidate->show_transfer_information;

    if (layout_changed && !reopen_main_gui(gui, server, candidate)) {
        if (reopen_main_gui(gui, server, &old_prefs)) {
            gui_message(*gui, "Preferences not applied",
                        "The main window could not be rebuilt with the requested transfer-information layout. The previous layout was restored.");
        } else {
            gui_message(NULL, "AmiDrop",
                        "The main window could not be reopened. Please restart AmiDrop.");
        }
        return FALSE;
    }

    if (was_running && port_changed) {
        server_stop(server);
        if (!server_start(server, candidate)) {
            char failure[224];
            BOOL restored;
            strncpy(failure, server->alert, sizeof(failure) - 1);
            failure[sizeof(failure) - 1] = '\0';
            restored = server_start(server, &old_prefs);
            gui_force_qr_redraw(*gui);
            if (restored) {
                gui_message(*gui, "Preferences not applied",
                            failure[0] ? failure : "The new server settings could not be applied. The previous settings were restored.");
            } else {
                gui_message(*gui, "Preferences not applied",
                            "The new server settings could not be applied, and the previous server settings could not be restarted either. The server remains stopped. Open Preferences and check the port, receive folder and network.");
            }
            server->alert[0] = '\0';
            if (layout_changed) reopen_main_gui(gui, server, &old_prefs);
            return FALSE;
        }
        gui_force_qr_redraw(*gui);
    } else {
        if (!server_apply_runtime_prefs(server, candidate)) {
            server->alert[0] = '\0';
            /* Put the previous layout back before claiming the previous
               settings are active, or the requester makes that claim over a
               window that has already switched to the rejected layout. */
            if (layout_changed) reopen_main_gui(gui, server, &old_prefs);
            /* Nothing was applied - not the port, not the size limit, not the
               checkboxes.  Reporting only the folder left the user believing
               the rest of their changes had been kept. */
            gui_message(*gui, "Preferences not applied",
                        "The receive folder could not be opened or created, so none of the changes were applied. The previous settings remain active.");
            return FALSE;
        }
    }

    *prefs = *candidate;
    if (save && !prefs_save(prefs)) {
        gui_message(*gui, "AmiDrop Preferences",
                    "The settings are active, but AmiDrop could not save them to ENVARC: or PROGDIR:.");
    } else if (save) {
        copy_status(server, "Preferences saved");
    } else {
        copy_status(server, "Preferences in use for this session");
    }
    return TRUE;
}

static BOOL handle_menu_action(struct AmiDropGui **gui, struct AmiDropServer *server,
                               struct AmiDropPrefs *prefs, ULONG action)
    __attribute__((noinline));
static BOOL handle_menu_action(struct AmiDropGui **gui, struct AmiDropServer *server,
                               struct AmiDropPrefs *prefs, ULONG action)
{
    if (action == MID_QUIT) {
        if (server->uploading &&
            !gui_confirm(*gui, "Quit AmiDrop",
                         "A transfer is active. Quit AmiDrop and remove the incomplete file?",
                         "Quit", "Cancel")) {
            return TRUE;
        }
        return FALSE;
    }

    if (action == MID_ABOUT) {
        gui_show_about(*gui);
    } else if (action == MID_START_SERVER) {
        if (server->running) {
            copy_status(server, "Server is already running");
        } else {
            server_start(server, prefs);
            gui_force_qr_redraw(*gui);
        }
    } else if (action == MID_STOP_SERVER) {
        if (!server->running) {
            copy_status(server, "Server is already stopped");
        } else if (!server->uploading ||
                   gui_confirm(*gui, "Stop AmiDrop server",
                               "A transfer is active. Stop the server and remove the incomplete file?",
                               "Stop", "Cancel")) {
            server_stop(server);
            gui_force_qr_redraw(*gui);
        }
    } else if (action == MID_PREFS) {
        struct AmiDropPrefs candidate;
        int prefs_action;
        if (server_is_uploading(server)) {
            gui_message(*gui, "AmiDrop Preferences",
                        "Preferences cannot be changed while a file is being received.");
        } else {
            candidate = *prefs;
            prefs_action = gui_preferences(*gui, &candidate);
            if (prefs_action == PREFS_ACTION_USE || prefs_action == PREFS_ACTION_SAVE) {
                apply_preferences(gui, server, prefs, &candidate,
                                  prefs_action == PREFS_ACTION_SAVE);
            }
        }
    }
    return TRUE;
}

static BOOL handle_gui_events(struct AmiDropGui **gui, struct AmiDropServer *server,
                              struct AmiDropPrefs *prefs)
{
    struct AmiDropGuiEvent event;
    BOOL keep_running = TRUE;

    while (keep_running && gui_next_event(*gui, &event)) {
        switch (event.type) {
            case GUI_EVENT_QUIT:
                keep_running = handle_menu_action(gui, server, prefs, MID_QUIT);
                break;

            case GUI_EVENT_ABORT:
                server_abort_upload(server, "Transfer aborted - partial file removed");
                break;

            case GUI_EVENT_CLEAR:
                if (!server_is_uploading(server)) {
                    server_clear_history(server);
                    copy_status(server, "Transfer list cleared");
                } else {
                    /* Refusing in silence made the button look dead. */
                    copy_status(server,
                                "Cannot clear the list while a file is being received");
                }
                break;

            case GUI_EVENT_MENU:
                keep_running = handle_menu_action(gui, server, prefs, event.value);
                break;

            case GUI_EVENT_REFRESH:
                gui_redraw(*gui, server, prefs);
                break;

            default:
                break;
        }
    }
    return keep_running;
}

int main(int argc, char **argv)
{
    static struct AmiDropPrefs prefs;
    static struct AmiDropServer server;
    struct AmiDropGui *gui;
    BOOL running = TRUE;
    BOOL previous_uploading = FALSE;
    ULONG last_alert_generation = 0;

    (void)version_string;
    prefs_load(&prefs);
    server_init_struct(&server);

    if (!open_libraries()) {
        if (IntuitionBase) {
            gui_message(NULL, "AmiDrop startup error",
                        "AmiDrop requires AmigaOS 3.0+, icon.library and bsdsocket.library.");
        } else {
            printf("AmiDrop: required AmigaOS libraries or bsdsocket.library are missing.\n");
        }
        close_libraries();
        return 20;
    }

    if (!claim_single_instance()) {
        gui_message(NULL, "AmiDrop", "AmiDrop is already running.");
        close_libraries();
        return 5;
    }

    if (!apply_launch_options(argc, argv, &prefs)) {
        gui_message(NULL, "AmiDrop ToolTypes",
                    "One or more ToolTypes/command-line settings were invalid and were ignored.\n\nValid examples:\nPORT=8080\nMAXSIZEKB=51200\nRECEIVEDIR=Work:Downloads/AmiDrop\nSTARTSERVER=YES\nIGNORESPACE=NO\nSHOWTRANSFERINFO=YES");
    }

    gui = gui_create(&prefs);
    if (!gui) {
        report_startup_failure();
        release_single_instance();
        close_libraries();
        return 20;
    }

    server_apply_runtime_prefs(&server, &prefs);
    if (prefs.start_server) server_start(&server, &prefs);
    else copy_status(&server, "Server stopped - use Project/Start server");

    gui_sync_history(gui, &server);
    gui_redraw(gui, &server, &prefs);
    present_server_alert(gui, &server, &last_alert_generation);

    while (running) {
        fd_set read_fds;
        LONG max_fd = -1;
        struct timeval timeout;
        ULONG signals;
        LONG wait_result;
        BOOL uploading;

        FD_ZERO(&read_fds);
        server_prepare_wait(&server, &read_fds, &max_fd);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        signals = gui_signal_mask(gui) | SIGBREAKF_CTRL_C;

        wait_result = WaitSelect(max_fd + 1, &read_fds, NULL, NULL, &timeout, &signals);
        (void)wait_result;

        if (signals & SIGBREAKF_CTRL_C) running = FALSE;
        if (running) {
            gui_signals_received(gui, signals);
            running = handle_gui_events(&gui, &server, &prefs);
        }
        /* After the events, not before.  handle_gui_events() runs the
           frontend's handlers, and the MUI one decides from the transfer
           state it was last told about whether it may open a requester.
           Moving these two above it would let a transfer begin without the
           frontend knowing until the next pass. */
        if (running) {
            server_process_ready(&server, &read_fds);
            server_check_timeout(&server);
        }

        uploading = server_is_uploading(&server);
        if (uploading != previous_uploading) {
            gui_set_abort_enabled(gui, uploading);
            previous_uploading = uploading;
            server.dirty = TRUE;
        }

        present_server_alert(gui, &server, &last_alert_generation);
        if (server.dirty) {
            gui_sync_history(gui, &server);
            gui_redraw(gui, &server, &prefs);
            server.dirty = FALSE;
        }
    }

    server_stop(&server);
    gui_destroy(gui);
    release_single_instance();
    close_libraries();
    return 0;
}
