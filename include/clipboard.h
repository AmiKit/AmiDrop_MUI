#ifndef AMIDROP_CLIPBOARD_H
#define AMIDROP_CLIPBOARD_H

#include <exec/types.h>

/* Puts one line of text on the Amiga clipboard, unit 0, as IFF FTXT.  Opens
   clipboard.device for the write and closes it again - there is nothing to
   keep open between two button presses.  Returns FALSE if the clipboard could
   not be written; a write that starts and then fails part way leaves the clip
   unterminated rather than untouched, so do not read FALSE as "nothing
   happened". */
BOOL amidrop_clip_put_text(const char *text);

#endif
