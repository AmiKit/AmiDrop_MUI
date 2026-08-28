#ifndef AMIDROP_SERVER_H
#define AMIDROP_SERVER_H

#include <exec/types.h>
#include <dos/dos.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "amidrop.h"

#define AMIDROP_CLIENT_NONE       0
#define AMIDROP_CLIENT_HEADERS    1
#define AMIDROP_CLIENT_UPLOAD     2

struct AmiDropServer {
    LONG listen_fd;
    LONG client_fd;
    UBYTE client_state;
    BOOL running;
    BOOL dirty;
    BOOL uploading;
    BOOL address_valid;
    BOOL ignore_free_space;

    char receive_dir[AMIDROP_PATH_MAX];
    UWORD port;
    ULONG max_file_bytes;

    char header_buffer[AMIDROP_HTTP_HEADER_MAX + 1];
    ULONG header_length;
    UBYTE io_buffer[AMIDROP_IO_BUFFER_SIZE];

    /* Upload filename parsing scratch space lives in the static server
       object instead of the 68k task stack. */
    char encoded_name[768];
    char decoded_name[512];
    char safe_name[256];

    BPTR upload_file;
    char temp_path[AMIDROP_PATH_MAX];
    char final_path[AMIDROP_PATH_MAX];
    char current_name[AMIDROP_NAME_MAX + 1];
    ULONG upload_total;
    ULONG upload_received;
    ULONG upload_started_at;
    ULONG last_activity_at;
    ULONG last_address_probe_at;

    char status[128];
    char address[96];
    char access_code[AMIDROP_ACCESS_CODE_LEN + 1];
    char session_token[AMIDROP_SESSION_TOKEN_LEN + 1];

    UWORD auth_failures;
    ULONG auth_block_until;

    char alert[224];
    ULONG alert_generation;

    struct AmiDropTransfer transfers[AMIDROP_TRANSFER_HISTORY];
    UWORD transfer_count;
    ULONG transfer_generation;
};

void server_init_struct(struct AmiDropServer *server);
BOOL server_start(struct AmiDropServer *server, const struct AmiDropPrefs *prefs);
void server_stop(struct AmiDropServer *server);
BOOL server_apply_runtime_prefs(struct AmiDropServer *server, const struct AmiDropPrefs *prefs);
void server_set_receive_dir(struct AmiDropServer *server, const char *dir);
void server_prepare_wait(const struct AmiDropServer *server, fd_set *read_fds, LONG *max_fd);
void server_process_ready(struct AmiDropServer *server, fd_set *read_fds);
void server_check_timeout(struct AmiDropServer *server);
void server_abort_upload(struct AmiDropServer *server, const char *reason);
BOOL server_is_uploading(const struct AmiDropServer *server);
void server_clear_history(struct AmiDropServer *server);
unsigned long long server_free_bytes_for_path(const char *path, BOOL *known);
unsigned long long server_required_free_bytes(unsigned long long file_bytes);

#endif
