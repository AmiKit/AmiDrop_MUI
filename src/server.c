#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "server.h"
#include "util.h"
#include "webpage.h"

extern struct Library *SocketBase;

static ULONG now_seconds(void)
{
    time_t now = time(NULL);
    if (now < 0) return 0;
    return (ULONG)now;
}

static void set_status(struct AmiDropServer *server, const char *text)
{
    if (!server) return;
    strncpy(server->status, text ? text : "", sizeof(server->status) - 1);
    server->status[sizeof(server->status) - 1] = '\0';
    server->dirty = TRUE;
}

static void set_status_name(struct AmiDropServer *server, const char *prefix, const char *name)
{
    if (!server) return;
    snprintf(server->status, sizeof(server->status), "%s%s", prefix ? prefix : "", name ? name : "");
    server->dirty = TRUE;
}

static void set_alert(struct AmiDropServer *server, const char *status, const char *alert)
{
    if (!server) return;
    if (status) set_status(server, status);
    strncpy(server->alert, alert ? alert : "AmiDrop encountered an error.", sizeof(server->alert) - 1);
    server->alert[sizeof(server->alert) - 1] = '\0';
    ++server->alert_generation;
    server->dirty = TRUE;
}

static void close_client(struct AmiDropServer *server)
{
    if (server->client_fd >= 0) {
        CloseSocket(server->client_fd);
        server->client_fd = -1;
    }
    server->client_state = AMIDROP_CLIENT_NONE;
    server->header_length = 0;
    server->header_scan_offset = 0;
}

static void cleanup_partial(struct AmiDropServer *server)
{
    if (server->upload_file) {
        Close(server->upload_file);
        server->upload_file = (BPTR)0;
    }
    if (server->temp_path[0]) {
        DeleteFile((STRPTR)server->temp_path);
    }
    server->temp_path[0] = '\0';
    server->uploading = FALSE;
    server->upload_total = 0;
    server->upload_received = 0;
    server->io_buffer_used = 0;
    server->last_progress_percent = 0;
    server->dirty = TRUE;
}

static BOOL send_all(LONG fd, const char *data, ULONG length)
{
    ULONG sent_total = 0;
    ULONG blocking = 0;

    IoctlSocket(fd, FIONBIO, (char *)&blocking);
    while (sent_total < length) {
        LONG sent = send(fd, (APTR)(data + sent_total), (LONG)(length - sent_total), 0);
        if (sent <= 0) return FALSE;
        sent_total += (ULONG)sent;
    }
    return TRUE;
}

static void send_response(struct AmiDropServer *server, LONG code, const char *reason,
                          const char *content_type, const char *body)
{
    char header[384];
    ULONG body_len = body ? (ULONG)strlen(body) : 0;
    int header_len;

    if (!server || server->client_fd < 0) return;
    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %ld %s\r\n"
        "Server: AmiDrop/%s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
        (long)code, reason ? reason : "", AMIDROP_VERSION,
        content_type ? content_type : "text/plain; charset=utf-8",
        (unsigned long)body_len);

    if (header_len > 0) send_all(server->client_fd, header, (ULONG)header_len);
    if (body_len > 0) send_all(server->client_fd, body, body_len);
    close_client(server);
}

static void send_index(struct AmiDropServer *server)
{
    send_response(server, 200, "OK", "text/html; charset=utf-8", amidrop_index_html);
}

static BOOL directory_exists(const char *path)
{
    BPTR lock;
    if (!path || !*path) return FALSE;
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

static BOOL ensure_directory(const char *path)
{
    BPTR lock;
    if (directory_exists(path)) return TRUE;
    lock = CreateDir((STRPTR)path);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

unsigned long long server_free_bytes_for_path(const char *path, BOOL *known)
{
    BPTR lock;
    struct InfoData info;
    unsigned long long blocks;
    unsigned long long bytes;

    if (known) *known = FALSE;
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) return 0;
    memset(&info, 0, sizeof(info));
    if (!Info(lock, &info)) {
        UnLock(lock);
        return 0;
    }
    UnLock(lock);

    blocks = (unsigned long long)(ULONG)info.id_NumBlocks;
    if ((unsigned long long)(ULONG)info.id_NumBlocksUsed >= blocks)
        blocks = 0;
    else
        blocks -= (unsigned long long)(ULONG)info.id_NumBlocksUsed;
    bytes = blocks * (unsigned long long)(ULONG)info.id_BytesPerBlock;
    if (known) *known = TRUE;
    return bytes;
}

unsigned long long server_required_free_bytes(unsigned long long file_bytes)
{
    unsigned long long reserve = file_bytes / 20ULL; /* five percent */
    if (reserve < 1048576ULL) reserve = 1048576ULL;
    if (reserve > 16777216ULL) reserve = 16777216ULL;
    return file_bytes + reserve;
}

static void join_path(char *dst, size_t dst_size, const char *dir, const char *name)
{
    size_t len;
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!dir) dir = "";
    if (!name) name = "";
    len = strlen(dir);
    if (len > 0 && (dir[len - 1] == ':' || dir[len - 1] == '/')) {
        snprintf(dst, dst_size, "%s%s", dir, name);
    } else {
        snprintf(dst, dst_size, "%s/%s", dir, name);
    }
}

static BOOL file_exists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

static void split_name(const char *name, char *base, size_t base_size, char *ext, size_t ext_size)
{
    const char *dot = strrchr(name, '.');
    size_t base_len;
    if (!dot || dot == name) {
        strncpy(base, name, base_size - 1);
        base[base_size - 1] = '\0';
        ext[0] = '\0';
        return;
    }
    base_len = (size_t)(dot - name);
    if (base_len >= base_size) base_len = base_size - 1;
    memcpy(base, name, base_len);
    base[base_len] = '\0';
    strncpy(ext, dot, ext_size - 1);
    ext[ext_size - 1] = '\0';
}

static void make_unique_name(struct AmiDropServer *server, const char *wanted,
                             char *out_name, size_t out_size)
{
    char test_path[AMIDROP_PATH_MAX];
    char base[AMIDROP_NAME_MAX + 1];
    char ext[AMIDROP_NAME_MAX + 1];
    ULONG n;

    strncpy(out_name, wanted, out_size - 1);
    out_name[out_size - 1] = '\0';
    join_path(test_path, sizeof(test_path), server->receive_dir, out_name);
    if (!file_exists(test_path)) return;

    split_name(wanted, base, sizeof(base), ext, sizeof(ext));
    for (n = 2; n < 10000; ++n) {
        char suffix[16];
        char candidate[AMIDROP_NAME_MAX + 1];
        size_t ext_len;
        size_t suffix_len;
        size_t room;
        snprintf(suffix, sizeof(suffix), "_%lu", (unsigned long)n);
        ext_len = strlen(ext);
        suffix_len = strlen(suffix);
        room = AMIDROP_NAME_MAX;
        if (ext_len < room) room -= ext_len; else room = 0;
        if (suffix_len < room) room -= suffix_len; else room = 0;
        if (room == 0) room = 1;
        snprintf(candidate, sizeof(candidate), "%.*s%s%s", (int)room, base, suffix, ext);
        join_path(test_path, sizeof(test_path), server->receive_dir, candidate);
        if (!file_exists(test_path)) {
            strncpy(out_name, candidate, out_size - 1);
            out_name[out_size - 1] = '\0';
            return;
        }
    }
}

static BOOL parse_content_length(const char *headers, ULONG *value)
{
    char text[64];
    char *end;
    unsigned long length;
    if (!amidrop_header_value(headers, "Content-Length", text, sizeof(text))) return FALSE;
    length = strtoul(text, &end, 10);
    if (end == text || *end != '\0') return FALSE;
    *value = (ULONG)length;
    return TRUE;
}

static ULONG credential_prng(ULONG state[4])
{
    ULONG t = state[3];
    ULONG s = state[0];

    state[3] = state[2];
    state[2] = state[1];
    state[1] = s;
    t ^= t << 11;
    t ^= t >> 8;
    state[0] = t ^ s ^ (s >> 19);
    return state[0];
}

static void generate_session_credentials(struct AmiDropServer *server)
{
    static const char token_alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    struct DateStamp stamp;
    ULONG state[4];
    ULONG random_value = 0;
    ULONG code;
    int available_bits = 0;
    int i;

    if (!server) return;

    memset(&stamp, 0, sizeof(stamp));
    DateStamp(&stamp);

    /*
     * AmiDrop's credentials are short-lived LAN credentials. Mix independent
     * launch-time values into a xorshift128 state so the QR token is much less
     * guessable than the human-friendly six-digit code. No credential is ever
     * persisted; both become invalid when AmiDrop is restarted.
     */
    state[0] = (ULONG)stamp.ds_Days ^ (ULONG)(APTR)server ^ 0xA341316CUL;
    state[1] = ((ULONG)stamp.ds_Minute << 16) ^ (ULONG)(APTR)FindTask(NULL) ^ 0xC8013EA4UL;
    state[2] = (ULONG)stamp.ds_Tick ^ (ULONG)time(NULL) ^ 0xAD90777DUL;
    state[3] = ((ULONG)server->listen_fd << 24) ^ (ULONG)(APTR)&stamp ^ 0x7E95761EUL;
    if ((state[0] | state[1] | state[2] | state[3]) == 0) state[0] = 1;

    code = 100000UL + (credential_prng(state) % 900000UL);
    snprintf(server->access_code, sizeof(server->access_code), "%06lu", (unsigned long)code);

    for (i = 0; i < AMIDROP_SESSION_TOKEN_LEN; ++i) {
        if (available_bits < 5) {
            random_value = credential_prng(state);
            available_bits = 30;
        }
        server->session_token[i] = token_alphabet[random_value & 31UL];
        random_value >>= 5;
        available_bits -= 5;
    }
    server->session_token[AMIDROP_SESSION_TOKEN_LEN] = '\0';
}

static int request_authorization(struct AmiDropServer *server, const char *headers)
{
    char supplied[64];
    ULONG now;
    BOOL valid = FALSE;

    if (!server || !headers) return 0;
    if (server->no_code_needed) return 1;
    now = now_seconds();
    if (server->auth_block_until && now && now < server->auth_block_until) return -1;
    if (server->auth_block_until && (!now || now >= server->auth_block_until)) {
        server->auth_block_until = 0;
        server->auth_failures = 0;
    }

    if (server->access_code[0] &&
        amidrop_header_value(headers, "X-AmiDrop-Code", supplied, sizeof(supplied)) &&
        amidrop_access_code_matches(supplied, server->access_code)) {
        valid = TRUE;
    } else if (server->session_token[0] &&
               amidrop_header_value(headers, "X-AmiDrop-Token", supplied, sizeof(supplied)) &&
               amidrop_session_token_matches(supplied, server->session_token)) {
        valid = TRUE;
    }

    if (valid) {
        server->auth_failures = 0;
        server->auth_block_until = 0;
        return 1;
    }

    ++server->auth_failures;
    if (server->auth_failures >= AMIDROP_AUTH_FAILURE_LIMIT) {
        server->auth_failures = 0;
        if (now) server->auth_block_until = now + AMIDROP_AUTH_LOCK_SECONDS;
        set_status(server, "Too many wrong codes - access locked for 30 seconds");
        return -1;
    }
    return 0;
}

static BOOL flush_upload_buffer(struct AmiDropServer *server)
{
    LONG written;

    if (!server || !server->upload_file) return FALSE;
    if (server->io_buffer_used == 0) return TRUE;

    written = Write(server->upload_file, server->io_buffer,
                    (LONG)server->io_buffer_used);
    if (written != (LONG)server->io_buffer_used) return FALSE;

    server->io_buffer_used = 0;
    return TRUE;
}

static BOOL append_upload_data(struct AmiDropServer *server,
                               const UBYTE *data, ULONG length)
{
    while (length > 0) {
        ULONG room;
        ULONG chunk;

        if (server->io_buffer_used >= AMIDROP_IO_BUFFER_SIZE &&
            !flush_upload_buffer(server)) {
            return FALSE;
        }

        room = AMIDROP_IO_BUFFER_SIZE - server->io_buffer_used;
        chunk = length < room ? length : room;
        if (chunk > 0) {
            CopyMem((APTR)data, server->io_buffer + server->io_buffer_used, chunk);
            server->io_buffer_used += chunk;
            data += chunk;
            length -= chunk;
        }

        if (server->io_buffer_used == AMIDROP_IO_BUFFER_SIZE &&
            !flush_upload_buffer(server)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void mark_progress_if_changed(struct AmiDropServer *server)
{
    ULONG percent;

    if (!server || !server->uploading || server->upload_total == 0) return;
    percent = (ULONG)amidrop_percent(server->upload_received, server->upload_total);
    if (percent != server->last_progress_percent) {
        server->last_progress_percent = percent;
        server->dirty = TRUE;
    }
}

static BOOL report_disk_write_failure(struct AmiDropServer *server)
{
    cleanup_partial(server);
    send_response(server, 507, "Insufficient Storage", "text/plain; charset=utf-8",
                  "Disk write failed.\n");
    set_alert(server, "Disk write failed - partial file removed",
              "Writing the received file failed. The partial file was removed. Check free space, write protection and that the target volume is still mounted.");
    return FALSE;
}

static BOOL prepare_upload(struct AmiDropServer *server, const char *headers, ULONG body_offset)
{
    char fitted_name[AMIDROP_NAME_MAX + 1];
    char unique_name[AMIDROP_NAME_MAX + 1];
    ULONG content_length;
    BOOL free_known;
    unsigned long long free_bytes;
    ULONG body_bytes = server->header_length - body_offset;

    if (!amidrop_header_value(headers, "X-AmiDrop-Filename", server->encoded_name, sizeof(server->encoded_name))) {
        send_response(server, 400, "Bad Request", "text/plain; charset=utf-8", "Missing X-AmiDrop-Filename header.\n");
        return FALSE;
    }
    if (!parse_content_length(headers, &content_length) || content_length == 0) {
        send_response(server, 400, "Bad Request", "text/plain; charset=utf-8", "Invalid Content-Length.\n");
        return FALSE;
    }
    if (content_length > server->max_file_bytes) {
        send_response(server, 413, "Payload Too Large", "text/plain; charset=utf-8", "File exceeds AmiDrop size limit.\n");
        return FALSE;
    }
    if (!amidrop_url_decode(server->encoded_name, server->decoded_name, sizeof(server->decoded_name))) {
        send_response(server, 400, "Bad Request", "text/plain; charset=utf-8", "Filename is too long.\n");
        return FALSE;
    }

    amidrop_sanitize_filename(server->decoded_name, server->safe_name, sizeof(server->safe_name));
    amidrop_fit_amiga_name(server->safe_name, fitted_name, sizeof(fitted_name), AMIDROP_NAME_MAX);
    make_unique_name(server, fitted_name, unique_name, sizeof(unique_name));

    if (!ensure_directory(server->receive_dir)) {
        send_response(server, 500, "Internal Server Error", "text/plain; charset=utf-8", "Receive directory is not available.\n");
        set_alert(server, "Receive directory unavailable",
                  "The receive folder is not available. Check that the volume is mounted and the drawer is writable, then choose another folder in Preferences.");
        return FALSE;
    }

    free_bytes = server_free_bytes_for_path(server->receive_dir, &free_known);
    if (!server->ignore_free_space && free_known &&
        free_bytes < server_required_free_bytes((unsigned long long)content_length)) {
        send_response(server, 507, "Insufficient Storage", "text/plain; charset=utf-8", "Not enough free space on the Amiga volume.\n");
        set_alert(server, "Not enough free disk space",
                  "The receive volume does not have enough free space for this file plus a small safety reserve. Choose a smaller limit/another volume, or enable 'Ignore free-space check' in Preferences.");
        return FALSE;
    }

    join_path(server->temp_path, sizeof(server->temp_path), server->receive_dir, AMIDROP_TEMP_NAME);
    join_path(server->final_path, sizeof(server->final_path), server->receive_dir, unique_name);
    DeleteFile((STRPTR)server->temp_path);
    server->upload_file = Open((STRPTR)server->temp_path, MODE_NEWFILE);
    if (!server->upload_file) {
        send_response(server, 500, "Internal Server Error", "text/plain; charset=utf-8", "Could not create temporary file.\n");
        set_alert(server, "Could not create temporary file",
                  "AmiDrop could not write to the receive folder. The volume may be missing, full or write-protected.");
        return FALSE;
    }

    strncpy(server->current_name, unique_name, sizeof(server->current_name) - 1);
    server->current_name[sizeof(server->current_name) - 1] = '\0';
    server->upload_total = content_length;
    server->upload_received = 0;
    server->io_buffer_used = 0;
    server->last_progress_percent = 0;
    server->uploading = TRUE;
    server->client_state = AMIDROP_CLIENT_UPLOAD;
    server->upload_started_at = now_seconds();
    server->last_activity_at = server->upload_started_at;
    set_status_name(server, "Receiving: ", server->current_name);

    if (body_bytes > content_length) body_bytes = content_length;
    if (body_bytes > 0) {
        if (!append_upload_data(server,
                                (const UBYTE *)(server->header_buffer + body_offset),
                                body_bytes)) {
            return report_disk_write_failure(server);
        }
        server->upload_received = body_bytes;
        server->last_activity_at = now_seconds();
        mark_progress_if_changed(server);
    }
    return TRUE;
}

static void format_transfer_display(char *dst, size_t dst_size, const char *name, ULONG bytes)
{
    if (!dst || dst_size == 0) return;
    if (bytes >= 1048576UL) {
        ULONG whole = bytes / 1048576UL;
        ULONG tenth = ((bytes % 1048576UL) * 10UL) / 1048576UL;
        snprintf(dst, dst_size, "%-30s  %lu.%lu MB", name ? name : "", (unsigned long)whole, (unsigned long)tenth);
    } else if (bytes >= 1024UL) {
        ULONG whole = bytes / 1024UL;
        ULONG tenth = ((bytes % 1024UL) * 10UL) / 1024UL;
        snprintf(dst, dst_size, "%-30s  %lu.%lu KB", name ? name : "", (unsigned long)whole, (unsigned long)tenth);
    } else {
        snprintf(dst, dst_size, "%-30s  %lu B", name ? name : "", (unsigned long)bytes);
    }
}

static void add_transfer_history(struct AmiDropServer *server, const char *name, ULONG bytes)
{
    UWORD new_count;
    UWORD i;

    if (!server || !name) return;
    new_count = server->transfer_count;
    if (new_count < AMIDROP_TRANSFER_HISTORY) ++new_count;

    for (i = new_count; i > 1; --i) {
        server->transfers[i - 1] = server->transfers[i - 2];
    }

    strncpy(server->transfers[0].name, name, sizeof(server->transfers[0].name) - 1);
    server->transfers[0].name[sizeof(server->transfers[0].name) - 1] = '\0';
    server->transfers[0].size = bytes;
    format_transfer_display(server->transfers[0].display,
                            sizeof(server->transfers[0].display), name, bytes);
    server->transfer_count = new_count;
    ++server->transfer_generation;
    server->dirty = TRUE;
}

static void finish_upload(struct AmiDropServer *server)
{
    char completed[AMIDROP_NAME_MAX + 1];
    BOOL close_ok = TRUE;

    strncpy(completed, server->current_name, sizeof(completed) - 1);
    completed[sizeof(completed) - 1] = '\0';

    /* Network reads are buffered in RAM; the final partial block must reach
       dos.library before the file is closed and renamed. */
    if (server->io_buffer_used > 0 && !flush_upload_buffer(server)) {
        report_disk_write_failure(server);
        return;
    }

    if (server->upload_file) {
        if (!Close(server->upload_file)) close_ok = FALSE;
        server->upload_file = (BPTR)0;
    }
    if (!close_ok) {
        DeleteFile((STRPTR)server->temp_path);
        server->temp_path[0] = '\0';
        server->uploading = FALSE;
        send_response(server, 507, "Insufficient Storage", "text/plain; charset=utf-8", "Could not flush received file to disk.\n");
        set_alert(server, "Could not flush received file - partial file removed",
                  "The file data could not be finalized on disk. Check free space, write protection and the target volume.");
        return;
    }
    if (!Rename((STRPTR)server->temp_path, (STRPTR)server->final_path)) {
        DeleteFile((STRPTR)server->temp_path);
        server->temp_path[0] = '\0';
        server->uploading = FALSE;
        send_response(server, 500, "Internal Server Error", "text/plain; charset=utf-8", "Could not finalize received file.\n");
        set_alert(server, "Could not finalize received file",
                  "The upload finished, but AmiDrop could not rename the temporary file to its final name. The temporary file was removed.");
        return;
    }

    server->temp_path[0] = '\0';
    server->uploading = FALSE;
    server->upload_received = server->upload_total;
    server->io_buffer_used = 0;
    server->last_progress_percent = 100;
    add_transfer_history(server, completed, server->upload_total);
    send_response(server, 201, "Created", "text/plain; charset=utf-8", "OK\n");
    set_status_name(server, "Received: ", completed);
}

static void send_config(struct AmiDropServer *server)
{
    char json[160];
    ULONG max_kb = server && server->max_file_bytes ? server->max_file_bytes / 1024UL : AMIDROP_DEFAULT_LIMIT_KB;
    snprintf(json, sizeof(json), "{\"maxFileKB\":%lu,\"version\":\"%s\",\"noCodeNeeded\":%s}\n",
             (unsigned long)max_kb, AMIDROP_VERSION,
             (server && server->no_code_needed) ? "true" : "false");
    send_response(server, 200, "OK", "application/json; charset=utf-8", json);
}

static void process_headers(struct AmiDropServer *server)
{
    const char *body;
    char method[12];
    char path[256];
    ULONG body_offset;

    server->header_buffer[server->header_length] = '\0';
    if (server->header_scan_offset > server->header_length)
        server->header_scan_offset = 0;

    /* Only the last three already-seen bytes can participate in a newly
       completed "\r\n\r\n" delimiter. Starting there avoids rescanning
       the whole header for every sender-chosen network fragment. */
    body = amidrop_find_header_end(server->header_buffer + server->header_scan_offset,
                                   server->header_length - server->header_scan_offset);
    if (!body) {
        server->header_scan_offset = server->header_length > 3
                                   ? server->header_length - 3 : 0;
        if (server->header_length >= AMIDROP_HTTP_HEADER_MAX) {
            send_response(server, 431, "Request Header Fields Too Large", "text/plain; charset=utf-8", "Request headers too large.\n");
        }
        return;
    }

    body_offset = (ULONG)(body - server->header_buffer);
    if (!amidrop_parse_request_line(server->header_buffer, method, sizeof(method), path, sizeof(path))) {
        send_response(server, 400, "Bad Request", "text/plain; charset=utf-8", "Invalid HTTP request.\n");
        return;
    }

    if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strncmp(path, "/?", 2) == 0)) {
        send_index(server);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/config") == 0) {
        send_config(server);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/favicon.ico") == 0) {
        send_response(server, 204, "No Content", "text/plain", "");
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/auth") == 0) {
        int auth = request_authorization(server, server->header_buffer);
        if (auth > 0) {
            send_response(server, 204, "No Content", "text/plain", "");
            set_status(server, "Phone/browser authorized - ready to receive");
        } else if (auth < 0) {
            send_response(server, 429, "Too Many Requests", "text/plain; charset=utf-8", "Too many invalid attempts. Try again in 30 seconds.\n");
        } else {
            send_response(server, 403, "Forbidden", "text/plain; charset=utf-8", "Invalid or expired AmiDrop credential.\n");
            set_status(server, "Access denied - invalid or expired credential");
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/upload") == 0) {
        int auth = request_authorization(server, server->header_buffer);
        if (auth < 0) {
            send_response(server, 429, "Too Many Requests", "text/plain; charset=utf-8", "Too many invalid attempts. Try again in 30 seconds.\n");
        } else if (auth == 0) {
            send_response(server, 403, "Forbidden", "text/plain; charset=utf-8", "Invalid or expired AmiDrop credential.\n");
            set_status(server, "Upload rejected - invalid or expired credential");
        } else if (prepare_upload(server, server->header_buffer, body_offset) &&
                   server->upload_received == server->upload_total) {
            finish_upload(server);
        }
    } else {
        send_response(server, 404, "Not Found", "text/plain; charset=utf-8", "Not found.\n");
    }
}

static void process_client_data(struct AmiDropServer *server)
{
    LONG received;

    if (server->client_state == AMIDROP_CLIENT_HEADERS) {
        ULONG room = AMIDROP_HTTP_HEADER_MAX - server->header_length;
        if (room == 0) {
            send_response(server, 431, "Request Header Fields Too Large", "text/plain; charset=utf-8", "Request headers too large.\n");
            return;
        }
        received = recv(server->client_fd, server->header_buffer + server->header_length, (LONG)room, 0);
        if (received == 0) {
            close_client(server);
            return;
        }
        if (received < 0) {
            close_client(server);
            return;
        }
        server->header_length += (ULONG)received;
        server->last_activity_at = now_seconds();
        process_headers(server);
        return;
    }

    if (server->client_state == AMIDROP_CLIENT_UPLOAD) {
        ULONG remaining = server->upload_total - server->upload_received;
        ULONG room;
        ULONG want;

        if (server->io_buffer_used >= AMIDROP_IO_BUFFER_SIZE &&
            !flush_upload_buffer(server)) {
            report_disk_write_failure(server);
            return;
        }

        room = AMIDROP_IO_BUFFER_SIZE - server->io_buffer_used;
        want = remaining < room ? remaining : room;
        if (want == 0) {
            finish_upload(server);
            return;
        }

        /* Read straight into the unused tail of the disk buffer. Small TCP
           fragments therefore accumulate without an extra CopyMem(). */
        received = recv(server->client_fd,
                        server->io_buffer + server->io_buffer_used,
                        (LONG)want, 0);
        if (received == 0) {
            cleanup_partial(server);
            close_client(server);
            set_alert(server, "Transfer interrupted - partial file removed",
                      "The sender disconnected before the file was complete. The incomplete file was removed safely.");
            return;
        }
        if (received < 0) {
            cleanup_partial(server);
            close_client(server);
            set_alert(server, "Network error - partial file removed",
                      "The network connection was lost while receiving a file. The incomplete file was removed safely.");
            return;
        }
        if (received > 0) {
            server->io_buffer_used += (ULONG)received;
            server->upload_received += (ULONG)received;
            server->last_activity_at = now_seconds();
            mark_progress_if_changed(server);

            if (server->io_buffer_used == AMIDROP_IO_BUFFER_SIZE &&
                !flush_upload_buffer(server)) {
                report_disk_write_failure(server);
                return;
            }
            if (server->upload_received >= server->upload_total) finish_upload(server);
        }
    }

}

static BOOL determine_address_from_socket(struct AmiDropServer *server, LONG fd);

static void accept_client(struct AmiDropServer *server)
{
    ULONG nonblocking = 1;
    LONG fd;

    if (server->client_fd >= 0) return;
    fd = accept(server->listen_fd, NULL, NULL);
    if (fd < 0) return;

    IoctlSocket(fd, FIONBIO, (char *)&nonblocking);
    server->client_fd = fd;
    server->client_state = AMIDROP_CLIENT_HEADERS;
    server->header_length = 0;
    server->header_scan_offset = 0;
    server->last_activity_at = now_seconds();
    if (determine_address_from_socket(server, fd)) server->dirty = TRUE;
}

static BOOL usable_ipv4_address(ULONG address)
{
    ULONG host_order = ntohl(address);

    if (address == INADDR_ANY) return FALSE;
    if ((host_order & 0xff000000UL) == 0x7f000000UL) return FALSE;
    return TRUE;
}

static BOOL set_display_address(struct AmiDropServer *server, ULONG address)
{
    const char *display;

    if (!server || !usable_ipv4_address(address)) return FALSE;
    display = (const char *)Inet_NtoA(address);
    if (!display || !display[0]) return FALSE;

    snprintf(server->address, sizeof(server->address),
             "http://%s:%u/", display, (unsigned)server->port);
    server->address_valid = TRUE;
    return TRUE;
}

static BOOL determine_address_from_socket(struct AmiDropServer *server, LONG fd)
{
    struct sockaddr_in local;
    socklen_t local_length = (socklen_t)sizeof(local);

    if (!server || fd < 0) return FALSE;
    memset(&local, 0, sizeof(local));
    if (getsockname(fd, (struct sockaddr *)&local, &local_length) < 0) return FALSE;
    if (local.sin_family != AF_INET) return FALSE;

    return set_display_address(server, local.sin_addr.s_addr);
}

static void determine_address(struct AmiDropServer *server)
{
    LONG probe_fd;
    struct sockaddr_in remote;
    char host[128];
    struct hostent *entry;

    if (!server) return;
    server->address[0] = '\0';
    server->address_valid = FALSE;

    /*
     * Ask the TCP/IP stack which local IPv4 address it would use for a
     * normal routed packet. connect() on a UDP socket does not transmit
     * anything here; it merely lets the stack select the active route.
     * This avoids Roadshow setups where the local host name resolves to
     * 127.0.0.1.
     */
    probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe_fd >= 0) {
        memset(&remote, 0, sizeof(remote));
        remote.sin_len = sizeof(remote);
        remote.sin_family = AF_INET;
        remote.sin_port = htons(9);
        remote.sin_addr.s_addr = htonl(0x08080808UL);

        if (connect(probe_fd, (struct sockaddr *)&remote, sizeof(remote)) == 0) {
            determine_address_from_socket(server, probe_fd);
        }
        CloseSocket(probe_fd);
    }

    if (server->address[0]) return;

    /* Compatibility fallback for stacks where hostname resolution is sane. */
    memset(host, 0, sizeof(host));
    if (gethostname((STRPTR)host, (LONG)sizeof(host) - 1) == 0 && host[0]) {
        entry = gethostbyname((STRPTR)host);
        if (entry && entry->h_addr_list && entry->h_addr_list[0]) {
            ULONG address = 0;
            CopyMem(entry->h_addr_list[0], &address, sizeof(address));
            if (set_display_address(server, address)) return;
        }
    }

    snprintf(server->address, sizeof(server->address),
             "http://<Amiga-IP>:%u/", (unsigned)server->port);
    server->address_valid = FALSE;
}

void server_init_struct(struct AmiDropServer *server)
{
    if (!server) return;
    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->client_fd = -1;
    strncpy(server->status, "Not started", sizeof(server->status) - 1);
}

BOOL server_start(struct AmiDropServer *server, const struct AmiDropPrefs *prefs)
{
    struct sockaddr_in address;
    LONG reuse = 1;
    ULONG nonblocking = 1;

    if (!server || !prefs) return FALSE;
    if (server->running) return TRUE;
    server->port = prefs->port;
    server->max_file_bytes = prefs->max_file_kb * 1024UL;
    server->no_code_needed = prefs->no_code_needed;
    server->ignore_free_space = prefs->ignore_free_space;
    server_set_receive_dir(server, prefs->receive_dir);
    server->auth_failures = 0;
    server->auth_block_until = 0;
    server->access_code[0] = '\0';
    server->session_token[0] = '\0';
    server->address[0] = '\0';
    server->address_valid = FALSE;
    server->header_scan_offset = 0;
    server->io_buffer_used = 0;
    server->last_progress_percent = 0;

    if (!ensure_directory(server->receive_dir)) {
        set_alert(server, "Cannot create/open receive directory",
                  "AmiDrop cannot use the configured receive folder. Check that the volume is mounted and writable, then choose another folder in Preferences.");
        return FALSE;
    }

    join_path(server->temp_path, sizeof(server->temp_path), server->receive_dir, AMIDROP_TEMP_NAME);
    DeleteFile((STRPTR)server->temp_path);
    server->temp_path[0] = '\0';

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        set_alert(server, "Could not create TCP socket",
                  "AmiDrop could not create a network socket. Check that your TCP/IP stack is online and bsdsocket.library is available.");
        return FALSE;
    }

    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(server->port);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        CloseSocket(server->listen_fd);
        server->listen_fd = -1;
        set_alert(server, "Port is already in use or bind failed",
                  "AmiDrop could not open its TCP port. Another program or another AmiDrop instance may already use it. Choose a different port in Preferences.");
        return FALSE;
    }
    if (listen(server->listen_fd, 4) < 0) {
        CloseSocket(server->listen_fd);
        server->listen_fd = -1;
        set_alert(server, "Could not listen on TCP port",
                  "AmiDrop opened the socket but could not start listening. Check the TCP/IP stack and try restarting the server.");
        return FALSE;
    }

    IoctlSocket(server->listen_fd, FIONBIO, (char *)&nonblocking);
    server->running = TRUE;
    determine_address(server);
    server->last_address_probe_at = now_seconds();
    generate_session_credentials(server);
    if (server->address_valid) {
        if (server->no_code_needed)
            set_status(server, "Ready - open address or scan QR (code disabled)");
        else
            set_status(server, "Ready - scan QR or open address and enter code");
    } else {
        set_alert(server, "Server running - no usable IPv4 address detected",
                  "AmiDrop is listening, but no active non-loopback IPv4 address was found. Check that the TCP/IP stack is online and the Amiga has an IP address.");
    }
    return TRUE;
}

void server_stop(struct AmiDropServer *server)
{
    if (!server) return;
    if (server->uploading) cleanup_partial(server);
    close_client(server);
    if (server->listen_fd >= 0) {
        CloseSocket(server->listen_fd);
        server->listen_fd = -1;
    }
    server->running = FALSE;
    server->address_valid = FALSE;
    server->address[0] = '\0';
    server->access_code[0] = '\0';
    server->session_token[0] = '\0';
    server->auth_failures = 0;
    server->auth_block_until = 0;
    set_status(server, "Server stopped");
}

BOOL server_apply_runtime_prefs(struct AmiDropServer *server, const struct AmiDropPrefs *prefs)
{
    if (!server || !prefs) return FALSE;
    if (!ensure_directory(prefs->receive_dir)) {
        set_alert(server, "Receive folder is not available",
                  "The selected receive folder cannot be created or opened. The previous folder remains active.");
        return FALSE;
    }
    server_set_receive_dir(server, prefs->receive_dir);
    server->max_file_bytes = prefs->max_file_kb * 1024UL;
    if (server->no_code_needed != prefs->no_code_needed) {
        server->auth_failures = 0;
        server->auth_block_until = 0;
    }
    server->no_code_needed = prefs->no_code_needed;
    server->ignore_free_space = prefs->ignore_free_space;
    server->dirty = TRUE;
    return TRUE;
}

void server_set_receive_dir(struct AmiDropServer *server, const char *dir)
{
    if (!server || !dir) return;
    strncpy(server->receive_dir, dir, sizeof(server->receive_dir) - 1);
    server->receive_dir[sizeof(server->receive_dir) - 1] = '\0';
}

void server_prepare_wait(const struct AmiDropServer *server, fd_set *read_fds, LONG *max_fd)
{
    if (!server || !read_fds || !max_fd) return;
    if (server->listen_fd >= 0 && server->client_fd < 0) {
        FD_SET(server->listen_fd, read_fds);
        if (server->listen_fd > *max_fd) *max_fd = server->listen_fd;
    }
    if (server->client_fd >= 0) {
        FD_SET(server->client_fd, read_fds);
        if (server->client_fd > *max_fd) *max_fd = server->client_fd;
    }
}

void server_process_ready(struct AmiDropServer *server, fd_set *read_fds)
{
    if (!server || !read_fds) return;
    if (server->client_fd < 0 && server->listen_fd >= 0 && FD_ISSET(server->listen_fd, read_fds)) {
        accept_client(server);
    }
    if (server->client_fd >= 0 && FD_ISSET(server->client_fd, read_fds)) {
        process_client_data(server);
    }
}

void server_check_timeout(struct AmiDropServer *server)
{
    ULONG now;
    if (!server) return;
    now = now_seconds();

    if (server->running && !server->address_valid && now &&
        (server->last_address_probe_at == 0 || now - server->last_address_probe_at >= 10UL)) {
        server->last_address_probe_at = now;
        determine_address(server);
        if (server->address_valid) {
            set_status(server, "Network detected - ready to receive");
        }
    }

    if (server->auth_block_until && now && now >= server->auth_block_until) {
        server->auth_block_until = 0;
        server->auth_failures = 0;
        if (!server->uploading && server->running) set_status(server, "Access unlocked - ready to receive");
    }

    if (server->client_fd < 0) return;
    if (now == 0 || server->last_activity_at == 0) return;
    if (now - server->last_activity_at >= AMIDROP_IDLE_TIMEOUT) {
        if (server->uploading) {
            cleanup_partial(server);
            close_client(server);
            set_alert(server, "Transfer timeout - partial file removed",
                      "No data arrived for 30 seconds. AmiDrop stopped the transfer and removed the incomplete file.");
        } else {
            close_client(server);
        }
    }
}

void server_reset_idle_timeout(struct AmiDropServer *server)
{
    ULONG now;

    if (!server || server->client_fd < 0) return;
    now = now_seconds();
    if (now) server->last_activity_at = now;
}

void server_abort_upload(struct AmiDropServer *server, const char *reason)
{
    if (!server || !server->uploading) return;
    cleanup_partial(server);
    if (server->client_fd >= 0) {
        send_response(server, 409, "Conflict", "text/plain; charset=utf-8", "Transfer aborted on the Amiga.\n");
    }
    set_status(server, reason ? reason : "Transfer aborted - partial file removed");
}


void server_clear_history(struct AmiDropServer *server)
{
    if (!server) return;
    memset(server->transfers, 0, sizeof(server->transfers));
    server->transfer_count = 0;
    ++server->transfer_generation;
    server->dirty = TRUE;
}

BOOL server_is_uploading(const struct AmiDropServer *server)
{
    return server && server->uploading;
}
