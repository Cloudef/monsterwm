/* see LICENSE for copyright and license */

#ifndef CONFIG_H
#define CONFIG_H

/** modifiers **/
#define MOD1            Mod1Mask    /* ALT key */
#define MOD4            Mod4Mask    /* Super/Windows key */
#define CONTROL         ControlMask /* Control key */
#define SHIFT           ShiftMask   /* Shift key */

/** generic settings **/
#define MASTER_SIZE     0.52
#define SHOW_PANEL      True      /* show panel by default on exec */
#define TOP_PANEL       True      /* False means panel is on bottom */
#define PANEL_HEIGHT    18        /* 0 for no space for panel, thus no panel */
#define DEFAULT_MODE    TILE      /* initial layout/mode: TILE MONOCLE BSTACK GRID FLOAT */
#define ATTACH_ASIDE    True      /* False means new window is master */
#define FOLLOW_WINDOW   False     /* follow the window when moved to a different desktop */
#define FOLLOW_MONITOR  True      /* follow the window when moved to a different monitor */
#define FOLLOW_MOUSE    False     /* focus the window the mouse just entered */
#define CLICK_TO_FOCUS  True      /* focus an unfocused window when clicked  */
#define FOCUS_BUTTON    Button1   /* mouse button to be used along with CLICK_TO_FOCUS */
#define BORDER_WIDTH    2         /* window border width */
#define FOCUS           "#ff950e" /* focused window border color   */
#define UNFOCUS         "#444444" /* unfocused window border color */
#define INFOCUS         "#9c3885" /* focused window border color on unfocused monitor */
#define MINWSZ          50        /* minimum window size in pixels */
#define DEFAULT_MONITOR 0         /* the monitor to focus initially */
#define DEFAULT_DESKTOP 0         /* the desktop to focus initially */
#define DESKTOPS        4         /* number of desktops - edit DESKTOPCHANGE keys to suit */

/**
 * layouts for monitors, terminate with -1
 */
static const int monitor1[] = { TILE, TILE, BSTACK, MONOCLE, -1 };
static const int monitor2[] = { BSTACK, BSTACK, BSTACK, BSTACK, -1 };

/**
 * monitor configuration
 */
static const MonitorCfg monitorcfg[] = {
    /* show panel, layouts */
    { True,  monitor1 },
    { False, monitor2 },
};

/**
 * open applications to specified monitor and desktop
 * with the specified properties.
 * if monitor is negative, then current is assumed
 * if desktop is negative, then current is assumed
 */
static const AppRule rules[] = { \
    /*  class     monitor  desktop  follow  float */
    { "MPlayer",     0,       3,    True,   False },
    { "mplayer2",    0,       3,    True,   False },
    { "torrent",     1,       1,    False,  False },
    { "rss",         1,       0,    False,  False },
    { "irc",         1,       0,    False,  False },
    { "dwb",         0,       0,    False,  False },
    { "Oblogout",    0 ,     -1,    True,   False  },
    { "qtermite",    0,      -1,    True,   True  },
    { "stalonetray", 0,      -1,    False,  True  },
};

/* helper for spawning shell commands */
#define SHCMD(cmd) {.com = (const char*[]){"/bin/sh", "-c", cmd, NULL}}

/** commands **/
static const char *termcmd[]     = { "termite",          NULL };
static const char *menucmd[]     = { "dmenu_run", "-p", "monsterwm", NULL };
static const char *qtermite[]    = { "qtermite",         NULL };
static const char *trayd[]       = { "stalonetrayd",     NULL };
static const char *oblogout[]    = { "oblogout",         NULL };
static const char *svolminus[]   = { "svol", "-d", "1",  NULL };
static const char *svolplus[]    = { "svol", "-i", "1",  NULL };
static const char *svolmute[]    = { "svol", "-t",       NULL };
static const char *loliclip[]    = { "lolictrl",         NULL };
static const char *lolicurl[]    = { "lolictrl", "-u",   NULL };
static const char *lolisync[]    = { "lolictrl", "-spc", NULL };
static const char *mpdtoggle[]   = { "lolimpd", "toggle", NULL };
static const char *lolimpd[]     = { "lolimpdnu", NULL };

#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)

/* PRNTS: file name syntax
 * PRNTF: fullscreen print command
 * PRNTW: window print command */
#define PRNTS "$HOME/monsterwm-$(date +'%H:%M-%d-%m-%Y').png"
#define PRNTF "ffcast -x "STR(DEFAULT_MONITOR)" % scrot -g %wx%h+%x+%y "PRNTS
#define PRNTW "scrotwin "PRNTS

#define MONITORCHANGE(K,N) \
    {  MOD4,             K,              change_monitor, {.i = N}}, \
    {  MOD4|ShiftMask,   K,              client_to_monitor, {.i = N}},

#define DESKTOPCHANGE(K,N) \
    {  MOD1,             K,              change_desktop, {.i = N}}, \
    {  MOD1|ShiftMask,   K,              client_to_desktop, {.i = N}},

/**
 * keyboard shortcuts
 */
static Key keys[] = {
    /* modifier          key            function           argument */
    {  MOD4,             XK_b,          togglepanel,       {NULL}},
    {  MOD4,             XK_BackSpace,  focusurgent,       {NULL}},
    {  MOD4,             XK_q,          killclient,        {NULL}},
    {  MOD4,             XK_j,          next_win,          {NULL}},
    {  MOD4,             XK_k,          prev_win,          {NULL}},
    {  MOD4,             XK_h,          resize_master,     {.i = -10}}, /* decrease size in px */
    {  MOD4,             XK_l,          resize_master,     {.i = +10}}, /* increase size in px */
    {  MOD4,             XK_u,          resize_stack,      {.i = -10}}, /* shrink   size in px */
    {  MOD4,             XK_i,          resize_stack,      {.i = +10}}, /* grow     size in px */
    {  MOD4|CONTROL,     XK_h,          rotate,            {.i = -1}},
    {  MOD4|CONTROL,     XK_l,          rotate,            {.i = +1}},
    {  MOD4|SHIFT,       XK_h,          rotate_filled,     {.i = -1}},
    {  MOD4|SHIFT,       XK_l,          rotate_filled,     {.i = +1}},
    {  MOD4,             XK_Tab,        last_desktop,      {NULL}},
    {  MOD4,             XK_Return,     swap_master,       {NULL}},
    {  MOD4|SHIFT,       XK_j,          move_down,         {NULL}},
    {  MOD4|SHIFT,       XK_k,          move_up,           {NULL}},
    {  MOD4|SHIFT,       XK_t,          switch_mode,       {.i = TILE}},
    {  MOD4|SHIFT,       XK_m,          switch_mode,       {.i = MONOCLE}},
    {  MOD4|SHIFT,       XK_b,          switch_mode,       {.i = BSTACK}},
    {  MOD4|SHIFT,       XK_g,          switch_mode,       {.i = GRID}},
    {  MOD4|SHIFT,       XK_f,          switch_mode,       {.i = FLOAT}},
    {  MOD4|CONTROL,     XK_r,          quit,              {.i = 0}}, /* quit with exit value 0 */
    {  MOD4|CONTROL,     XK_q,          quit,              {.i = 1}}, /* quit with exit value 1 */
    {  MOD4|SHIFT,       XK_Return,     spawn,             {.com = termcmd}},
    {  MOD4,             XK_p,          spawn,             {.com = menucmd}},
    {  MOD4,             XK_Down,       moveresize,        {.v = (int []){   0,  25,   0,   0 }}}, /* move up    */
    {  MOD4,             XK_Up,         moveresize,        {.v = (int []){   0, -25,   0,   0 }}}, /* move down  */
    {  MOD4,             XK_Right,      moveresize,        {.v = (int []){  25,   0,   0,   0 }}}, /* move right */
    {  MOD4,             XK_Left,       moveresize,        {.v = (int []){ -25,   0,   0,   0 }}}, /* move left  */
    {  MOD4|SHIFT,       XK_Down,       moveresize,        {.v = (int []){   0,   0,   0,  25 }}}, /* height grow   */
    {  MOD4|SHIFT,       XK_Up,         moveresize,        {.v = (int []){   0,   0,   0, -25 }}}, /* height shrink */
    {  MOD4|SHIFT,       XK_Right,      moveresize,        {.v = (int []){   0,   0,  25,   0 }}}, /* width grow    */
    {  MOD4|SHIFT,       XK_Left,       moveresize,        {.v = (int []){   0,   0, -25,   0 }}}, /* width shrink  */
       DESKTOPCHANGE(    XK_F1,                             0)
       DESKTOPCHANGE(    XK_F2,                             1)
       DESKTOPCHANGE(    XK_F3,                             2)
       DESKTOPCHANGE(    XK_F4,                             3)

    /* monitor shortcuts */
       MONITORCHANGE(    XK_comma,                          0)
       MONITORCHANGE(    XK_period,                         1)
       MONITORCHANGE(    XK_F1,                             0)
       MONITORCHANGE(    XK_F2,                             1)

    { 0,                 XK_Print,      spawn,             SHCMD(PRNTF) },
    { MOD1,              XK_Print,      spawn,             SHCMD(PRNTW) },
    { MOD4,              XK_Page_Down,  spawn,             {.v = svolminus } },
    { MOD4,              XK_Page_Up,    spawn,             {.v = svolplus  } },
    { MOD4|CONTROL,      XK_m,          spawn,             {.v = svolmute  } },
    { 0,                 XK_section,    spawn,             {.v = qtermite  } },
    { MOD4,              XK_t,          spawn,             {.v = trayd     } },
    { MOD4,              XK_Escape,     spawn,             {.v = oblogout  } },
    { MOD4,              XK_c,          spawn,             {.v = loliclip  } },
    { MOD4|SHIFT,        XK_c,          spawn,             {.v = lolicurl  } },
    { MOD1|SHIFT,        XK_c,          spawn,             {.v = lolisync  } },
    { 0,                 XK_Pause,      spawn,             {.v = mpdtoggle } },
    { MOD4,              XK_m,          spawn,             {.v = lolimpd   } },
    { MOD4,              XK_F12,        togglefullscreen,  {NULL}},
};

/**
 * mouse shortcuts
 */
static Button buttons[] = {
    {  MOD4,    Button1,     mousemotion,   {.i = MOVE}},
    {  MOD4,    Button3,     mousemotion,   {.i = RESIZE}},
};
#endif

/* vim: set expandtab ts=4 sts=4 sw=4 : */
