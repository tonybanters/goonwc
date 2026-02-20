#ifndef DWC_CONFIG_H
#define DWC_CONFIG_H

#include <xkbcommon/xkbcommon-keysyms.h>
#include <owl/owl.h>

/* appearance */
static const int gap_inner  = 10;
static const int gap_outer  = 10;
static const float mfact    = 0.55; /* master area size [0.05..0.95] */

/* tagging */
#define TAGCOUNT 9

/* commands */
static const char *termcmd[]  = { "foot", NULL };
static const char *menucmd[]  = { "dmenu_run", NULL };

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
} Arg_Cmd;

typedef struct {
	unsigned int tag;
} Arg_Tag;

typedef struct {
	int i;
} Arg_Int;

/* function declarations for keybindings */
void spawn(void *arg);
void killclient(void *arg);
void quit(void *arg);
void view(void *arg);
void tag(void *arg);
void toggleview(void *arg);
void toggletag(void *arg);
void focusstack(void *arg);
void setmfact(void *arg);
void togglefloating(void *arg);
void zoom(void *arg);

/* tag arguments */
static Arg_Tag tag1 = { .tag = 1 << 0 };
static Arg_Tag tag2 = { .tag = 1 << 1 };
static Arg_Tag tag3 = { .tag = 1 << 2 };
static Arg_Tag tag4 = { .tag = 1 << 3 };
static Arg_Tag tag5 = { .tag = 1 << 4 };
static Arg_Tag tag6 = { .tag = 1 << 5 };
static Arg_Tag tag7 = { .tag = 1 << 6 };
static Arg_Tag tag8 = { .tag = 1 << 7 };
static Arg_Tag tag9 = { .tag = 1 << 8 };

/* other arguments */
static Arg_Cmd arg_term = { .cmd = termcmd };
static Arg_Cmd arg_menu = { .cmd = menucmd };
static Arg_Int arg_focusup   = { .i = -1 };
static Arg_Int arg_focusdown = { .i = +1 };
static Arg_Int arg_mfact_dec = { .i = -5 };  /* -5% */
static Arg_Int arg_mfact_inc = { .i = +5 };  /* +5% */

static Key keys[] = {
	/* modifier    key         function        argument */
	{ MOD,         XKB_KEY_p,  spawn,          &arg_menu },
	{ MOD,         XKB_KEY_Return, spawn,      &arg_term },
	{ MOD,         XKB_KEY_q,  killclient,     NULL },
	{ MOD|OWL_MOD_SHIFT, XKB_KEY_Q, quit,      NULL },

	{ MOD,         XKB_KEY_j,  focusstack,     &arg_focusdown },
	{ MOD,         XKB_KEY_k,  focusstack,     &arg_focusup },
	{ MOD,         XKB_KEY_h,  setmfact,       &arg_mfact_dec },
	{ MOD,         XKB_KEY_l,  setmfact,       &arg_mfact_inc },
	{ MOD,         XKB_KEY_space, togglefloating, NULL },
	{ MOD,         XKB_KEY_Tab, zoom,          NULL },

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
