/* FreeRDP: Licensed under the Apache License, Version 2.0. */
#include <freerdp/config.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <freerdp/input.h>
#include <freerdp/client.h>
#include "../xf_keyboard.h"
#include "../xf_event.h"

/* Run the real X11 keyboard/focus/button handlers with a deterministic X server
 * and input sink. No display, window manager, RDP server or sleeps are needed. */
static char trace[1024];
static size_t used;
static BOOL failAlt;
static char physical[32];
static const struct
{
	KeySym sym;
	KeyCode code;
	DWORD scan;
	char name;
} keys[] = { { XK_Alt_L, 64, RDP_SCANCODE_LMENU, 'A' },
             { XK_Alt_R, 108, RDP_SCANCODE_RMENU, 'R' },
             { XK_Shift_L, 50, RDP_SCANCODE_LSHIFT, 'S' },
             { XK_Shift_R, 62, RDP_SCANCODE_RSHIFT, 'H' },
             { XK_Control_L, 37, RDP_SCANCODE_LCONTROL, 'C' },
             { XK_Control_R, 105, RDP_SCANCODE_RCONTROL, 'D' },
             { XK_Super_L, 133, RDP_SCANCODE_LWIN, 'L' },
             { XK_Super_R, 134, RDP_SCANCODE_RWIN, 'G' },
             { XK_f, 41, RDP_SCANCODE_KEY_F, 'F' },
             { XK_Tab, 23, RDP_SCANCODE_TAB, 'T' } };

static void record(char name, char direction)
{
	WINPR_ASSERT(used + 2 < sizeof(trace));
	trace[used++] = name;
	trace[used++] = direction;
	trace[used] = '\0';
}

KeyCode XKeysymToKeycode(Display* display, KeySym sym)
{
	WINPR_UNUSED(display);
	for (size_t i = 0; i < ARRAYSIZE(keys); i++)
	{
		if (keys[i].sym == sym)
			return keys[i].code;
	}
	return 0;
}

int XQueryKeymap(Display* display, char map[32])
{
	WINPR_UNUSED(display);
	memcpy(map, physical, sizeof(physical));
	return 1;
}

Bool XQueryPointer(Display* display, Window window, Window* root, Window* child, int* rx, int* ry,
                   int* x, int* y, unsigned int* mask)
{
	WINPR_UNUSED(display);
	WINPR_UNUSED(window);
	*root = *child = None;
	*rx = *ry = *x = *y = 0;
	*mask = 0;
	return False;
}

int XSync(Display* display, Bool discard)
{
	WINPR_UNUSED(display);
	WINPR_UNUSED(discard);
	return 1;
}

int XUngrabKeyboard(Display* display, Time time)
{
	WINPR_UNUSED(display);
	WINPR_UNUSED(time);
	return 1;
}

BOOL freerdp_input_send_keyboard_event_ex(rdpInput* input, BOOL down, BOOL repeat, UINT32 scan)
{
	WINPR_UNUSED(input);
	if (failAlt && down && (scan == RDP_SCANCODE_LMENU))
		return FALSE;
	for (size_t i = 0; i < ARRAYSIZE(keys); i++)
	{
		if (keys[i].scan == scan)
		{
			record(keys[i].name, down ? (repeat ? '*' : '+') : '-');
			return TRUE;
		}
	}
	record('?', down ? '+' : '-');
	return TRUE;
}

BOOL freerdp_input_send_focus_in_event(rdpInput* input, UINT16 flags)
{
	WINPR_UNUSED(input);
	WINPR_UNUSED(flags);
	return TRUE;
}

BOOL freerdp_client_send_button_event(rdpClientContext* context, BOOL relative, UINT16 flags,
                                      INT32 x, INT32 y)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(relative);
	WINPR_UNUSED(x);
	WINPR_UNUSED(y);
	record((flags & PTR_FLAGS_MOVE) ? 'M' : 'B', (flags & PTR_FLAGS_DOWN) ? '+' : '-');
	return TRUE;
}

BOOL freerdp_client_send_extended_button_event(rdpClientContext* context, BOOL relative,
                                               UINT16 flags, INT32 x, INT32 y)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(relative);
	WINPR_UNUSED(x);
	WINPR_UNUSED(y);
	record('E', (flags & PTR_XFLAGS_DOWN) ? '+' : '-');
	return TRUE;
}

BOOL freerdp_client_send_wheel_event(rdpClientContext* context, UINT16 flags)
{
	WINPR_UNUSED(context);
	WINPR_UNUSED(flags);
	record('W', '+');
	return TRUE;
}

static void key(xfContext* xfc, KeySym sym, BOOL down)
{
	XKeyEvent event = { 0 };
	event.keycode = XKeysymToKeycode(xfc->display, sym);
	if (down)
	{
		physical[event.keycode / 8] |= (char)(1 << (event.keycode % 8));
		xf_keyboard_key_press(xfc, &event, sym);
	}
	else
	{
		physical[event.keycode / 8] &= (char)~(1 << (event.keycode % 8));
		xf_keyboard_key_release(xfc, &event, sym);
	}
}

static BOOL focus(xfContext* xfc, int type, int mode)
{
	XEvent event = { 0 };
	event.xfocus.type = type;
	event.xfocus.mode = mode;
	return xf_event_process(xfc->common.context.instance, &event);
}

static BOOL expect(const char* label, const char* expected)
{
	if (strcmp(trace, expected) == 0)
		return TRUE;
	fprintf(stderr, "%s: expected '%s', got '%s'\n", label, expected, trace);
	return FALSE;
}

#define CHECK(label, expected)        \
	do                                \
	{                                 \
		if (!expect(label, expected)) \
			goto out;                 \
	} while (0)
#define RESET()                                                          \
	do                                                                   \
	{                                                                    \
		memset(xfc.KeyboardState, 0, sizeof(xfc.KeyboardState));         \
		memset(xfc.KeyboardForwarded, 0, sizeof(xfc.KeyboardForwarded)); \
		memset(&xfc.deferredAlt, 0, sizeof(xfc.deferredAlt));            \
		memset(physical, 0, sizeof(physical));                           \
		used = 0;                                                        \
		trace[0] = '\0';                                                 \
	} while (0)

int main(int argc, char* argv[])
{
	WINPR_UNUSED(argc);
	WINPR_UNUSED(argv);
	int rc = 1;
	xfContext xfc = { 0 };
	xfWindow window = { 0 };
	XModifierKeymap modifiers = { 0 };
	freerdp instance = { 0 };
	rdpInput input = { 0 };
	rdpSettings* settings = freerdp_settings_new(0);
	if (!settings)
		return 1;
	if (!freerdp_settings_set_bool(settings, FreeRDP_UnicodeInput, FALSE))
		goto out;
	xfc.common.context.settings = settings;
	xfc.common.context.input = &input;
	xfc.common.context.instance = &instance;
	instance.context = &xfc.common.context;
	xfc.window = &window;
	xfc.display = (Display*)&instance; /* All X calls used in this test are mocked. */
	xfc.modifierMap = &modifiers;
	xfc.log = WLog_Get("com.freerdp.client.x11.test.deferred-alt");
	for (size_t i = 0; i < ARRAYSIZE(keys); i++)
		xfc.X11_KEYCODE_TO_VIRTUAL_SCANCODE[keys[i].code] = keys[i].scan;
	xfc.button_map[0].button = 1;
	xfc.button_map[0].flags = PTR_FLAGS_BUTTON1;
	xfc.button_map[1].button = 4;
	xfc.button_map[1].flags = PTR_FLAGS_WHEEL | 120;
	xfc.button_map[2].button = 8;
	xfc.button_map[2].flags = PTR_XFLAGS_BUTTON1;
	key(&xfc, XK_Alt_L, TRUE);
	CHECK("default sends immediately", "A+");
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("default Alt tap", "A+A-");
	if (!freerdp_settings_set_bool(settings, FreeRDP_KeyboardDeferAlt, TRUE))
		goto out;
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Alt_L, TRUE);
	CHECK("pending repeats", "");
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("bare tap", "A+A-");
	RESET();
	if (!freerdp_settings_set_bool(settings, FreeRDP_UnicodeInput, TRUE))
		goto out;
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("Unicode Alt tap uses scancodes", "A+A-");
	if (!freerdp_settings_set_bool(settings, FreeRDP_UnicodeInput, FALSE))
		goto out;
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("Alt+F ordering and repeat", "A+F+F*F-A-");
	RESET();
	key(&xfc, XK_Shift_L, TRUE);
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	key(&xfc, XK_Shift_L, FALSE);
	CHECK("Shift before Alt", "S+A+F+F-A-S-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Shift_L, TRUE);
	key(&xfc, XK_Shift_L, TRUE);
	CHECK("Alt+Shift pending", "");
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Shift_L, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("Alt before Shift", "A+S+F+F-S-A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Control_L, TRUE);
	key(&xfc, XK_Control_L, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("modifier flush has no duplicate Ctrl", "A+C+C-A-");
	RESET();
	key(&xfc, XK_Alt_R, TRUE);
	CHECK("right Alt immediate", "R+");
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Alt_R, FALSE);
	CHECK("right Alt chord", "R+F+F-R-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	if (!focus(&xfc, FocusOut, NotifyGrab))
		goto out;
	key(&xfc, XK_Alt_L, FALSE);
	if (!focus(&xfc, FocusIn, NotifyUngrab))
		goto out;
	CHECK("host grab consumes Tab without focus switch", "");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Shift_L, TRUE);
	if (!focus(&xfc, FocusOut, NotifyGrab))
		goto out;
	if (!focus(&xfc, FocusOut, NotifyNormal))
		goto out;
	key(&xfc, XK_Shift_L, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	if (!focus(&xfc, FocusIn, NotifyNormal))
		goto out;
	key(&xfc, XK_Tab, FALSE);
	CHECK("host Alt+Shift+Tab and unmatched Tab release", "");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	if (!focus(&xfc, FocusOut, NotifyNormal))
		goto out;
	if (!focus(&xfc, FocusIn, NotifyNormal))
		goto out;
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("return with Alt held, repeat then release", "");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	if (!focus(&xfc, FocusOut, NotifyGrab))
		goto out;
	if (!focus(&xfc, FocusIn, NotifyUngrab))
		goto out;
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("return held Alt then guest chord", "A+F+F-A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_f, TRUE);
	if (!focus(&xfc, FocusOut, NotifyWhileGrabbed))
		goto out;
	CHECK("cleanup releases only forwarded keys in order", "A+F+F-A-");
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	if (!focus(&xfc, FocusIn, NotifyNormal))
		goto out;
	CHECK("cleanup does not repeat releases", "A+F+F-A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	if (!xf_generic_MotionNotify(&xfc, 10, 20, None, FALSE))
		goto out;
	CHECK("pointer motion leaves Alt pending", "M-");
	if (!xf_generic_ButtonEvent(&xfc, 10, 20, 1, None, FALSE, TRUE))
		goto out;
	if (!xf_generic_ButtonEvent(&xfc, 10, 20, 1, None, FALSE, FALSE))
		goto out;
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("Alt+button", "M-A+B+B-A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	if (!xf_generic_ButtonEvent(&xfc, 10, 20, 4, None, FALSE, FALSE))
		goto out;
	CHECK("wheel release is not input", "");
	if (!xf_generic_ButtonEvent(&xfc, 10, 20, 4, None, FALSE, TRUE))
		goto out;
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("Alt+wheel", "A+W+A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	if (!xf_generic_RawButtonEvent(&xfc, 8, FALSE, TRUE))
		goto out;
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("Alt+raw extended button", "A+E+A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Shift_L, TRUE);
	key(&xfc, XK_Shift_L, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("queued Shift tap preserves order", "A+S+S-A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Shift_L, TRUE);
	if (!focus(&xfc, FocusOut, NotifyGrab))
		goto out;
	if (!focus(&xfc, FocusIn, NotifyUngrab))
		goto out;
	CHECK("resync never manufactures modifier taps", "");
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	key(&xfc, XK_Shift_L, FALSE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("return with Alt and Shift physically held", "A+S+F+F-S-A-");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	xf_keyboard_discard_deferred_alt(&xfc);
	xfc.deferredAlt.blocked = FALSE; /* Local action without losing focus. */
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("local action suppresses unused Alt tap", "");
	RESET();
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_f, TRUE);
	key(&xfc, XK_f, FALSE);
	xf_keyboard_discard_deferred_alt(&xfc);
	xfc.deferredAlt.blocked = FALSE;
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("local cancellation retains already forwarded Alt release", "A+F+F-A-");
	RESET();
	failAlt = TRUE;
	key(&xfc, XK_Alt_L, TRUE);
	key(&xfc, XK_Alt_L, FALSE);
	CHECK("failed Alt-down never produces Alt-up", "");
	failAlt = FALSE;
	rc = 0;
out:
	freerdp_settings_free(settings);
	return rc;
}
