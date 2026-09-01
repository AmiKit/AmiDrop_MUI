#include <exec/types.h>
#include <exec/io.h>
#include <devices/clipboard.h>

#include <proto/exec.h>

#include "clipboard.h"
#include "util.h"

/* The only thing AmiDrop copies is its own link, held in a 96-byte field in
   the frontend, plus the 20 bytes of IFF wrapper.  A local buffer of this size
   keeps the whole operation allocation-free.  160 holds 140 characters, so
   nothing enforces the pairing: let that field carry more than 140 and this
   quietly starts refusing - which fails closed, but silently. */
#define AMIDROP_CLIP_MAX 160

BOOL amidrop_clip_put_text(const char *text)
{
    struct MsgPort *port;
    struct IOClipReq *io;
    UBYTE clip[AMIDROP_CLIP_MAX];
    size_t size;
    BOOL ok = FALSE;

    size = amidrop_build_ftxt(clip, sizeof(clip), text);
    if (size == 0) return FALSE;

    port = CreateMsgPort();
    if (!port) return FALSE;

    io = (struct IOClipReq *)CreateIORequest(port, sizeof(*io));
    if (io) {
        if (OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP,
                       (struct IORequest *)io, 0) == 0) {
            /* clipboard.doc/CMD_WRITE: zero both for an initial write, and the
               device assigns the clip id itself. */
            io->io_ClipID = 0;
            io->io_Offset = 0;
            io->io_Data = (STRPTR)clip;
            io->io_Length = (ULONG)size;
            io->io_Command = CMD_WRITE;

            if (DoIO((struct IORequest *)io) == 0 &&
                io->io_Actual == (ULONG)size) {
                /* Without CMD_UPDATE the clip is written but never becomes
                   the current one, so nothing can paste it. */
                io->io_Command = CMD_UPDATE;
                io->io_Length = 0;
                io->io_Data = NULL;
                if (DoIO((struct IORequest *)io) == 0) ok = TRUE;
            }
            CloseDevice((struct IORequest *)io);
        }
        DeleteIORequest((struct IORequest *)io);
    }

    DeleteMsgPort(port);
    return ok;
}
