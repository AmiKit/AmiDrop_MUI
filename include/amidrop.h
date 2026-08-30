#ifndef AMIDROP_H
#define AMIDROP_H

#include <exec/types.h>

#define AMIDROP_NAME                 "AmiDrop"
#define AMIDROP_VERSION              "1.0.1"
#define AMIDROP_VERSION_NUMBER       "1.0.1"
#define AMIDROP_DATE                 "29.08.2026"
#define AMIDROP_DEFAULT_PORT         8080
#define AMIDROP_DEFAULT_LIMIT_KB     51200UL  /* 50 MB */
#define AMIDROP_MIN_LIMIT_KB         512UL
#define AMIDROP_SAFE_MAX_LIMIT_KB    1048576UL /* 1 GB */
#define AMIDROP_DEFAULT_DIR          "PROGDIR:Downloads"
#define AMIDROP_DEFAULT_START_SERVER TRUE
#define AMIDROP_PREFS_FILE           "ENVARC:AmiDrop.prefs"
#define AMIDROP_FALLBACK_PREFS       "PROGDIR:AmiDrop.prefs"
#define AMIDROP_TEMP_NAME            "ADROP.PART"
#define AMIDROP_IDLE_TIMEOUT         30
#define AMIDROP_AUTH_FAILURE_LIMIT   5
#define AMIDROP_AUTH_LOCK_SECONDS    30
#define AMIDROP_HTTP_HEADER_MAX      8192
#define AMIDROP_IO_BUFFER_SIZE       32768
#define AMIDROP_PATH_MAX             256
#define AMIDROP_NAME_MAX             30
#define AMIDROP_ACCESS_CODE_LEN      6
#define AMIDROP_SESSION_TOKEN_LEN    20
#define AMIDROP_TRANSFER_HISTORY     50
#define AMIDROP_TRANSFER_DISPLAY_MAX 80
#define AMIDROP_QR_VERSION           3
#define AMIDROP_QR_BUFFER_SIZE       128
#define AMIDROP_QR_MAX_PAYLOAD       52
#define AMIDROP_INSTANCE_PORT        "AmiDrop.SingleInstance"

struct AmiDropPrefs {
    char receive_dir[AMIDROP_PATH_MAX];
    UWORD port;
    ULONG max_file_kb;
    BOOL start_server;
    BOOL ignore_free_space;
};

struct AmiDropTransfer {
    char name[AMIDROP_NAME_MAX + 1];
    ULONG size;
    char display[AMIDROP_TRANSFER_DISPLAY_MAX];
};

#endif
