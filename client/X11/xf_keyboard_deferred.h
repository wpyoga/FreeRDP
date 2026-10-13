/* FreeRDP: Licensed under the Apache License, Version 2.0. */
#ifndef FREERDP_CLIENT_X11_KEYBOARD_DEFERRED_H
#define FREERDP_CLIENT_X11_KEYBOARD_DEFERRED_H

#include <X11/Xlib.h>
#include <winpr/wtypes.h>

typedef BOOL (*xfDeferredSend)(void* context, BOOL down, BOOL repeat, const XKeyEvent* event);

typedef struct
{
	BOOL held;
	BOOL sent;
	BOOL canceled;
	BOOL blocked;
	XKeyEvent alt;
	/* Only Shift is queued: flushing it immediately would break host Alt+Shift+Tab. */
	XKeyEvent shift[2];
	size_t count;
} xfDeferredAlt;

void xf_deferred_alt_discard(xfDeferredAlt* state);
void xf_deferred_alt_resume(xfDeferredAlt* state, const XKeyEvent* alt, BOOL held);
void xf_deferred_alt_flush(xfDeferredAlt* state, xfDeferredSend send, void* context);
BOOL xf_deferred_alt_key(xfDeferredAlt* state, BOOL alt, BOOL shift, BOOL down, BOOL repeat,
                         const XKeyEvent* event, xfDeferredSend send, void* context);

#endif
