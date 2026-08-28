#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
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

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *GadToolsBase = NULL;
struct Library *AslBase = NULL;
struct Library *IconBase = NULL;
struct Library *SocketBase = NULL;

static struct MsgPort *instance_port = NULL;
static const char version_string[] __attribute__((used)) =
    "$VER: AmiDrop " AMIDROP_VERSION " (" AMIDROP_DATE ")";

static BOOL open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (!IntuitionBase) return FALSE;
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39);
    AslBase = OpenLibrary((STRPTR)"asl.library", 39);
    IconBase = OpenLibrary((STRPTR)"icon.library", 39);
    SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);

    return GfxBase && GadToolsBase && AslBase && IconBase && SocketBase;
}

static void close_libraries(void)
{
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
    if (IconBase) { CloseLibrary(IconBase); IconBase = NULL; }
    if (AslBase) { CloseLibrary(AslBase); AslBase = NULL; }
    if (GadToolsBase) { CloseLibrary(GadToolsBase); GadToolsBase = NULL; }
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
    static const char *keys[] = { "PORT", "MAXSIZE", "MAXSIZEKB", "RECEIVEDIR", "STARTSERVER", "IGNORESPACE" };
    UWORD i;
    BOOL all_valid = TRUE;

    if (!prefs || !tooltypes) return TRUE;
    for (i = 0; i < 6; ++i) {
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

static BOOL apply_preferences(struct AmiDropGui *gui, struct AmiDropServer *server,
                              struct AmiDropPrefs *prefs,
                              const struct AmiDropPrefs *candidate, BOOL save)
{
    struct AmiDropPrefs old_prefs;
    BOOL was_running;
    BOOL port_changed;

    if (!gui || !server || !prefs || !candidate) return FALSE;
    old_prefs = *prefs;
    was_running = server->running;
    port_changed = old_prefs.port != candidate->port;

    if (was_running && port_changed) {
        server_stop(server);
        if (!server_start(server, candidate)) {
            char failure[224];
            BOOL restored;
            strncpy(failure, server->alert, sizeof(failure) - 1);
            failure[sizeof(failure) - 1] = '\0';
            restored = server_start(server, &old_prefs);
            gui_force_qr_redraw(gui);
            if (restored) {
                gui_message(gui, "Preferences not applied",
                            failure[0] ? failure : "The new server settings could not be applied. The previous settings were restored.");
            } else {
                gui_message(gui, "Preferences not applied",
                            "The new server settings could not be applied, and the previous server settings could not be restarted either. The server remains stopped. Open Preferences and check the port, receive folder and network.");
            }
            server->alert[0] = '\0';
            return FALSE;
        }
        gui_force_qr_redraw(gui);
    } else {
        if (!server_apply_runtime_prefs(server, candidate)) return FALSE;
    }

    *prefs = *candidate;
    if (save && !prefs_save(prefs)) {
        gui_message(gui, "AmiDrop Preferences",
                    "The settings are active, but AmiDrop could not save them to ENVARC: or PROGDIR:.");
    } else if (save) {
        copy_status(server, "Preferences saved");
    } else {
        copy_status(server, "Preferences in use for this session");
    }
    return TRUE;
}

static BOOL handle_menu_action(struct AmiDropGui *gui, struct AmiDropServer *server,
                               struct AmiDropPrefs *prefs, ULONG action)
{
    if (action == MID_QUIT) {
        if (server->uploading &&
            !gui_confirm(gui, "Quit AmiDrop",
                         "A transfer is active. Quit AmiDrop and remove the incomplete file?",
                         "Quit", "Cancel")) {
            return TRUE;
        }
        return FALSE;
    }

    if (action == MID_ABOUT) {
        gui_show_about(gui);
    } else if (action == MID_START_SERVER) {
        if (server->running) {
            copy_status(server, "Server is already running");
        } else {
            server_start(server, prefs);
            gui_force_qr_redraw(gui);
        }
    } else if (action == MID_STOP_SERVER) {
        if (!server->running) {
            copy_status(server, "Server is already stopped");
        } else if (!server->uploading ||
                   gui_confirm(gui, "Stop AmiDrop server",
                               "A transfer is active. Stop the server and remove the incomplete file?",
                               "Stop", "Cancel")) {
            server_stop(server);
            gui_force_qr_redraw(gui);
        }
    } else if (action == MID_PREFS) {
        struct AmiDropPrefs candidate;
        int prefs_action;
        if (server_is_uploading(server)) {
            gui_message(gui, "AmiDrop Preferences",
                        "Preferences cannot be changed while a file is being received.");
        } else {
            candidate = *prefs;
            prefs_action = gui_preferences(gui, &candidate);
            if (prefs_action == PREFS_ACTION_USE || prefs_action == PREFS_ACTION_SAVE) {
                apply_preferences(gui, server, prefs, &candidate,
                                  prefs_action == PREFS_ACTION_SAVE);
            }
        }
    }
    return TRUE;
}

static BOOL handle_gui_messages(struct AmiDropGui *gui, struct AmiDropServer *server,
                                struct AmiDropPrefs *prefs)
{
    struct IntuiMessage *message;
    BOOL keep_running = TRUE;

    while ((message = GT_GetIMsg(gui->window->UserPort)) != NULL) {
        ULONG msg_class = message->Class;
        UWORD code = message->Code;
        APTR iaddress = message->IAddress;
        GT_ReplyIMsg(message);

        if (msg_class == IDCMP_CLOSEWINDOW) {
            keep_running = handle_menu_action(gui, server, prefs, MID_QUIT);
        } else if (msg_class == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(gui->window);
            gui_force_qr_redraw(gui);
            gui_redraw(gui, server, prefs);
            GT_EndRefresh(gui->window, TRUE);
        } else if (msg_class == IDCMP_MENUPICK) {
            UWORD selection = code;
            while (selection != MENUNULL && keep_running) {
                UWORD next_selection = MENUNULL;
                ULONG action = gui_menu_action(gui, selection, &next_selection);
                keep_running = handle_menu_action(gui, server, prefs, action);
                selection = next_selection;
            }
        } else if (msg_class == IDCMP_GADGETUP) {
            struct Gadget *gadget = (struct Gadget *)iaddress;
            if (!gadget) continue;

            if (gadget->GadgetID == GID_QUIT) {
                keep_running = handle_menu_action(gui, server, prefs, MID_QUIT);
            } else if (gadget->GadgetID == GID_ABORT) {
                server_abort_upload(server, "Transfer aborted - partial file removed");
            } else if (gadget->GadgetID == GID_CLEAR) {
                if (!server_is_uploading(server)) {
                    server_clear_history(server);
                    copy_status(server, "Transfer list cleared");
                }
            }
        }
    }
    return keep_running;
}

int main(int argc, char **argv)
{
    static struct AmiDropPrefs prefs;
    static struct AmiDropServer server;
    static struct AmiDropGui gui;
    BOOL running = TRUE;
    BOOL previous_uploading = FALSE;
    ULONG last_alert_generation = 0;

    (void)version_string;
    prefs_load(&prefs);
    server_init_struct(&server);

    if (!open_libraries()) {
        if (IntuitionBase) {
            gui_message(NULL, "AmiDrop startup error",
                        "AmiDrop requires AmigaOS 3.1+, gadtools.library, asl.library, icon.library and bsdsocket.library.");
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
                    "One or more ToolTypes/command-line settings were invalid and were ignored.\n\nValid examples:\nPORT=8080\nMAXSIZEKB=51200\nRECEIVEDIR=Work:Downloads/AmiDrop\nSTARTSERVER=YES\nIGNORESPACE=NO");
    }

    if (!gui_open(&gui)) {
        gui_message(NULL, "AmiDrop startup error", "Could not open the GadTools window.");
        release_single_instance();
        close_libraries();
        return 20;
    }

    server_apply_runtime_prefs(&server, &prefs);
    if (prefs.start_server) server_start(&server, &prefs);
    else copy_status(&server, "Server stopped - use Project/Start server");

    gui_sync_history(&gui, &server);
    gui_redraw(&gui, &server, &prefs);
    present_server_alert(&gui, &server, &last_alert_generation);

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
        signals = gui.signal_mask | SIGBREAKF_CTRL_C;

        wait_result = WaitSelect(max_fd + 1, &read_fds, NULL, NULL, &timeout, &signals);
        (void)wait_result;

        if (signals & SIGBREAKF_CTRL_C) running = FALSE;
        if (running && (signals & gui.signal_mask)) {
            running = handle_gui_messages(&gui, &server, &prefs);
        }
        if (running) {
            server_process_ready(&server, &read_fds);
            server_check_timeout(&server);
        }

        uploading = server_is_uploading(&server);
        if (uploading != previous_uploading) {
            gui_set_abort_enabled(&gui, uploading);
            previous_uploading = uploading;
            server.dirty = TRUE;
        }

        present_server_alert(&gui, &server, &last_alert_generation);
        if (server.dirty) {
            gui_sync_history(&gui, &server);
            gui_redraw(&gui, &server, &prefs);
            server.dirty = FALSE;
        }
    }

    server_stop(&server);
    gui_close(&gui);
    release_single_instance();
    close_libraries();
    return 0;
}
