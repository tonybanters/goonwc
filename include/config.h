#ifndef DWC_CONFIG_H
#define DWC_CONFIG_H

#include <xkbcommon/xkbcommon-keysyms.h>
#include <owl/owl.h>

/* appearance */
static const int gap = 5;                     /* gap size in pixels */
static const float default_proportion = 0.5;  /* window width as fraction of screen */

/* tagging */
#define TAGCOUNT 9

/* commands */
static const char *termcmd[]  = { "foot", NULL };
static const char *menucmd[]  = { "rofi", "-show", "drun", NULL };

/* key definitions */
#define MOD OWL_MOD_ALT

typedef struct {
	uint32_t mod;
	uint32_t keysym;
	void (*func)(void *arg);
	void *arg;
} Key;

typedef struct {
	const char **cmd;
} arg_cmd;

typedef struct {
	unsigned int tag;
} arg_tag;

typedef struct {
	int i;
} arg_int;

/* function declarations for keybindings */
void spawn(void *arg);
void killclient(void *arg);
void quit(void *arg);
void view(void *arg);
void tag(void *arg);
void toggleview(void *arg);
void toggletag(void *arg);
void focusstack(void *arg);
void setproportion(void *arg);
void maximize(void *arg);
void togglefloating(void *arg);

/* tag arguments */
static arg_tag tag1 = { .tag = 1 << 0 };
static arg_tag tag2 = { .tag = 1 << 1 };
static arg_tag tag3 = { .tag = 1 << 2 };
static arg_tag tag4 = { .tag = 1 << 3 };
static arg_tag tag5 = { .tag = 1 << 4 };
static arg_tag tag6 = { .tag = 1 << 5 };
static arg_tag tag7 = { .tag = 1 << 6 };
static arg_tag tag8 = { .tag = 1 << 7 };
static arg_tag tag9 = { .tag = 1 << 8 };

/* other arguments */
static arg_cmd arg_term = { .cmd = termcmd };
static arg_cmd arg_menu = { .cmd = menucmd };
static arg_int arg_focusup   = { .i = -1 };
static arg_int arg_focusdown = { .i = +1 };
static arg_int arg_prop_dec = { .i = -10 };  /* shrink window 10% */
static arg_int arg_prop_inc = { .i = +10 };  /* grow window 10% */

static Key keys[] = {
	/* modifier    key         function        argument */
	{ MOD,         XKB_KEY_p,  spawn,          &arg_menu },
	{ MOD,         XKB_KEY_Return, spawn,      &arg_term },
	{ MOD,         XKB_KEY_q,  killclient,     NULL },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_Q, quit,      NULL },

	{ MOD,         XKB_KEY_h,  focusstack,     &arg_focusup },
	{ MOD,         XKB_KEY_l,  focusstack,     &arg_focusdown },
	{ MOD,         XKB_KEY_f,  maximize,       NULL },
	{ MOD,         XKB_KEY_space, togglefloating, NULL },

	{ MOD,         XKB_KEY_1,  view,           &tag1 },
	{ MOD,         XKB_KEY_2,  view,           &tag2 },
	{ MOD,         XKB_KEY_3,  view,           &tag3 },
	{ MOD,         XKB_KEY_4,  view,           &tag4 },
	{ MOD,         XKB_KEY_5,  view,           &tag5 },
	{ MOD,         XKB_KEY_6,  view,           &tag6 },
	{ MOD,         XKB_KEY_7,  view,           &tag7 },
	{ MOD,         XKB_KEY_8,  view,           &tag8 },
	{ MOD,         XKB_KEY_9,  view,           &tag9 },

	{ MOD|OWL_MOD_SHIFT, XKB_KEY_exclam,      tag, &tag1 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_at,          tag, &tag2 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_numbersign,  tag, &tag3 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_dollar,      tag, &tag4 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_percent,     tag, &tag5 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_asciicircum, tag, &tag6 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_ampersand,   tag, &tag7 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_asterisk,    tag, &tag8 },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_parenleft,   tag, &tag9 },

	{ MOD|OWL_MOD_CTRL,  XKB_KEY_1, toggleview, &tag1 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_2, toggleview, &tag2 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_3, toggleview, &tag3 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_4, toggleview, &tag4 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_5, toggleview, &tag5 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_6, toggleview, &tag6 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_7, toggleview, &tag7 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_8, toggleview, &tag8 },
	{ MOD|OWL_MOD_CTRL,  XKB_KEY_9, toggleview, &tag9 },
};

#endif
