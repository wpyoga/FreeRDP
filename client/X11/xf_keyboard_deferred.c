/* FreeRDP: Licensed under the Apache License, Version 2.0. */
#include "xf_keyboard_deferred.h"

void xf_deferred_alt_discard(xfDeferredAlt* state)
{
	state->canceled = TRUE;
	state->blocked = TRUE;
	state->count = 0;
	/* Keep sent until cleanup/resume, or the release after a local action.
	 * Discarding an unused Alt must not forget an Alt already sent. */
}

void xf_deferred_alt_resume(xfDeferredAlt* state, const XKeyEvent* alt, BOOL held)
{
	state->blocked = FALSE;
	state->held = held;
	state->sent = FALSE;
	state->canceled = TRUE;
	state->alt = *alt;
	state->count = 0;
}

void xf_deferred_alt_flush(xfDeferredAlt* state, xfDeferredSend send, void* context)
{
	if (state->blocked || !state->held || state->sent)
		return;
	/* A new guest action may use Alt still held after returning from a host shortcut. */
	if (!send(context, TRUE, FALSE, &state->alt))
		return;
	state->sent = TRUE;
	for (size_t i = 0; i < state->count; i++)
		(void)send(context, TRUE, FALSE, &state->shift[i]);
	state->count = 0;
}

BOOL xf_deferred_alt_key(xfDeferredAlt* state, BOOL alt, BOOL shift, BOOL down, BOOL repeat,
                         const XKeyEvent* event, xfDeferredSend send, void* context)
{
	if (state->blocked)
		return TRUE;
	if (alt)
	{
		if (down)
		{
			if (!state->held)
			{
				state->alt = *event;
				state->held = TRUE;
				state->canceled = FALSE;
			}
			else if (state->sent)
				(void)send(context, TRUE, repeat, event);
		}
		else
		{
			if (state->held && !state->canceled)
				xf_deferred_alt_flush(state, send, context);
			if (state->sent)
				(void)send(context, FALSE, FALSE, event);
			state->held = FALSE;
			state->sent = FALSE;
			state->count = 0;
		}
		return TRUE;
	}
	if (shift && down && state->held && !state->sent)
	{
		for (size_t i = 0; i < state->count; i++)
		{
			if (state->shift[i].keycode == event->keycode)
				return TRUE;
		}
		if (state->count < 2)
			state->shift[state->count++] = *event;
		return TRUE;
	}
	return FALSE;
}
