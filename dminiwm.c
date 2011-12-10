/* Based on:
 * - catwm at https://github.com/pyknite/catwm
 * - 2wm at http://hg.suckless.org/2wm/
 * - dwm at http://dwm.suckless.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>

#define LENGTH(x) (sizeof(x)/sizeof(*x))

enum {
    WM_PROTOCOLS,
    WM_DELETE_WINDOW,
    _NET_WM_WINDOW_TYPE,
    _NET_WM_WINDOW_TYPE_UTILITY,
    _NET_WM_WINDOW_TYPE_DOCK,
    _NET_WM_WINDOW_TYPE_SPLASH,
    _NET_WM_WINDOW_TYPE_DIALOG,
    _NET_WM_WINDOW_TYPE_NOTIFICATION,
    ATOM_COUNT
};

/* layout modes */
enum {
    TILE,
    MONOCYCLE,
    BSTACK,
    GRID,
};

/* structs */
typedef union {
    const char** com;
    const int i;
} Arg;

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*function)(const Arg arg);
    const Arg arg;
} key;

typedef struct client {
    struct client *next;
    struct client *prev;
    Window win;
} client;

typedef struct {
    int master_size;
    int mode;
    int growth;
    client *head;
    client *current;
} desktop;

typedef struct {
    const char *class;
    const int desktop;
    const Bool follow;
} AppRule;

/* Functions */
static void add_window(Window w);
static void buttonpressed(XEvent *e);
static void change_desktop(const Arg arg);
static void client_to_desktop(const Arg arg);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void enternotify(XEvent *e);
static void die(const char* errstr, ...);
static int xerrorstart(Display *dis, XErrorEvent *ee);
static unsigned long getcolor(const char* color);
static void grabkeys(void);
static void keypress(XEvent *e);
static void killclient(const Arg arg);
static void maprequest(XEvent *e);
static void move_down(const Arg arg);
static void move_up(const Arg arg);
static void rotate_desktop(const Arg arg);
static void next_win(const Arg arg);
static void prev_win(const Arg arg);
static void cleanup(void);
static void quit(const Arg arg);
static void removeclient(client *c);
static void resize_master(const Arg arg);
static void resize_stack(const Arg arg);
static void save_desktop(int i);
static void select_desktop(int i);
static void deletewindow(Window w);
static void setup(void);
static void sigchld(int unused);
static void spawn(const Arg arg);
static void run(void);
static void swap_master(const Arg arg);
static void tile(void);
static void last_desktop(const Arg arg);
static void switch_mode(const Arg arg);
static void update_current(void);

#include "config.h"

/* variables */
static Display *dis;
static Bool running = True;
static int retval = 0;
static int current_desktop = 0;
static int previous_desktop = 0;
static int growth = 0;
static int mode = DEFAULT_MODE;
static int master_size;
static int sh;
static int sw;
static int screen;
static Window root;
static int xerror(Display *dis, XErrorEvent *ee);
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int win_focus;
static unsigned int win_unfocus;
static unsigned int numlockmask = 0; /* dynamic key lock mask */
static unsigned int nrules = LENGTH(rules);
static client *head    = NULL;
static client *current = NULL;
static Atom atoms[ATOM_COUNT];

/* events array */
static void (*events[LASTEvent])(XEvent *e) = {
    [KeyPress] = keypress,
    [MapRequest] = maprequest,
    [EnterNotify] = enternotify,
    [ButtonPress] = buttonpressed,
    [DestroyNotify] = destroynotify,
    [ConfigureNotify] = configurenotify,
    [ConfigureRequest] = configurerequest
};

/* desktops array */
static desktop desktops[DESKTOPS];

/* ***************************** Window Management ******************************* */
void add_window(Window w) {
    client *c, *t;

    if(!(c = (client *)calloc(1, sizeof(client))))
        die("error: could not calloc() %u bytes\n", sizeof(client));

    if(head == NULL) {
        c->win = w;
        head = c;
    } else if(ATTACH_ASIDE == 0) {
        for(t=head; t->next; t=t->next);
        c->prev = t;
        c->win = w;
        t->next = c;
    } else {
        t = head;
        c->next = t;
        t->prev = c;
        c->win = w;
        head = c;
    }

    current = c;
    save_desktop(current_desktop);

    if(FOLLOW_MOUSE == 0)
        XSelectInput(dis, c->win, EnterWindowMask);
}

void removeclient(client *c) {
    if(c->prev == NULL) {       /* w is head */
        if(c->next == NULL) {   /* head is only window on screen */
            free(head);
            head = NULL;
        } else {                /* more windows on screen */
            head->next->prev = NULL;
            head = head->next;
        }
        current = head;
    } else {                    /* w is on stack */
        if(c->next == NULL) {   /* w is last window on screen */
            c->prev->next = NULL;
        } else {                /* w is somewhere in the middle */
            c->next->prev = c->prev;
            c->prev->next = c->next;
        }
        current = c->prev;
    }

    save_desktop(current_desktop);
    tile();
    update_current();
}

void killclient(const Arg arg) {
    if(current == NULL) return;
    deletewindow(current->win);
    removeclient(current);
}

void next_win(const Arg arg) {
    if(current == NULL || head == NULL) return;
    current = (current->next == NULL) ? head : current->next;
    update_current();
}

void prev_win(const Arg arg) {
    if(current == NULL || head == NULL) return;
    if(current->prev == NULL) /* if(current == head) */
        for(current=head; current->next; current=current->next);
    else
        current = current->prev;
    update_current();
}

void move_down(const Arg arg) {
    if(current == NULL || current == head || current->next == NULL)
        return;
    Window tmpwin = current->win;
    current->win = current->next->win;
    current->next->win = tmpwin;
    current = current->next;
    save_desktop(current_desktop);
    tile();
    update_current();
}

void move_up(const Arg arg) {
    if(current == NULL || current == head || current->prev == head)
        return;
    Window tmpwin = current->win;
    current->win = current->prev->win;
    current->prev->win = tmpwin;
    current = current->prev;
    save_desktop(current_desktop);
    tile();
    update_current();
}

void swap_master(const Arg arg) {
    if(head->next == NULL || current == NULL || mode == MONOCYCLE)
        return;
    Window tmpwin = head->win;
    current = (current == head) ? head->next : current;
    head->win = current->win;
    current->win = tmpwin;
    current = head;
    save_desktop(current_desktop);
    tile();
    update_current();
}

/* **************************** Desktop Management ************************************* */
void change_desktop(const Arg arg) {
    if(arg.i == current_desktop) return;
    previous_desktop = current_desktop;

    /* save current desktop settings and unmap windows */
    save_desktop(current_desktop);
    for(client *c=head; c; c=c->next)
        XUnmapWindow(dis, c->win);
    /* read new desktop properties and map new windows */
    select_desktop(arg.i);
    for(client *c=head; c; c=c->next)
        XMapWindow(dis, c->win);

    tile();
    update_current();
}

void last_desktop(const Arg arg) {
    change_desktop((Arg){.i = previous_desktop});
}

void rotate_desktop(const Arg arg) {
    change_desktop((Arg){.i = (current_desktop + DESKTOPS + arg.i) % DESKTOPS});
}

void client_to_desktop(const Arg arg) {
    if(arg.i == current_desktop || current == NULL)
        return;

    client *cc = current;
    int cd = current_desktop;

    // Add client to desktop
    select_desktop(arg.i);
    add_window(cc->win);
    save_desktop(arg.i);

    // Remove client from current desktop
    select_desktop(cd);
    XUnmapWindow(dis, cc->win);
    removeclient(cc);
    save_desktop(cd);
    tile();
    update_current();

    if(FOLLOW_WINDOW == 0)
        change_desktop(arg);
}

void save_desktop(int i) {
    desktops[i].master_size = master_size;
    desktops[i].mode = mode;
    desktops[i].growth = growth;
    desktops[i].head = head;
    desktops[i].current = current;
}

void select_desktop(int i) {
    master_size = desktops[i].master_size;
    mode = desktops[i].mode;
    growth = desktops[i].growth;
    head = desktops[i].head;
    current = desktops[i].current;
    current_desktop = i;
}

void tile(void) {
    if(head == NULL) return; /* no need to arange anything */

    client *c;
    int n = 0;
    int x = 0;
    int y = (TOP_PANEL) ? 0 : PANEL_HEIGHT;

    if(head->next == NULL) {
        XMoveResizeWindow(dis, head->win, 0, y, sw + 2*BORDER_WIDTH, sh + 2*BORDER_WIDTH);
        return;
    }

    switch(mode) {
        case TILE:
            /* master window */
            XMoveResizeWindow(dis, head->win, 0, y, master_size - BORDER_WIDTH, sh - BORDER_WIDTH);
            /* stack */
            for(n=0, c=head->next; c; c=c->next, ++n);  /* count windows */
            growth = (n == 1) ? 0 : growth;             /* if only one window don't care about growth */
            XMoveResizeWindow(dis, head->next->win, master_size + BORDER_WIDTH, y, sw - master_size - 2*BORDER_WIDTH, sh/n + growth - BORDER_WIDTH);
            y += sh/n + growth;
            for(c=head->next->next; c; c=c->next) {
                XMoveResizeWindow(dis, c->win, master_size + BORDER_WIDTH, y, sw - master_size - 2*BORDER_WIDTH, sh/n - growth/(n-1) - BORDER_WIDTH);
                y += sh/n - growth / (n-1);
            }
            break;
        case MONOCYCLE:
            for(c=head; c; c=c->next)
                XMoveResizeWindow(dis, c->win, 0, y, sw + 2*BORDER_WIDTH, sh + 2*BORDER_WIDTH);
            break;
        case BSTACK:
            /* master window */
            XMoveResizeWindow(dis, head->win, 0, y, sw - BORDER_WIDTH, master_size - BORDER_WIDTH);
            /* stack */
            for(n=0, c=head->next; c; c=c->next, ++n);  /* count windows */
            growth = (n == 1) ? 0 : growth;             /* if only one window don't care about growth */
            XMoveResizeWindow(dis, head->next->win, 0, y + master_size + BORDER_WIDTH, sw/n + growth - BORDER_WIDTH, sh - master_size - 2*BORDER_WIDTH);
            x = sw/n + growth;
            for(c=head->next->next; c; c=c->next) {
                XMoveResizeWindow(dis, c->win, x, y + master_size + BORDER_WIDTH, sw/n - growth/(n-1) - BORDER_WIDTH, sh-master_size - 2*BORDER_WIDTH);
                x += sw/n - growth/(n-1);
            }
            break;
        case GRID:
            {
                int xpos = 0;
                int wdt = 0;
                int ht = 0;

                for(c=head; c; c=c->next) ++x;

                for(c=head; c; c=c->next) {
                    ++n;
                    if(x >= 7) {
                        wdt = sw/3 - BORDER_WIDTH;
                        ht  = sh/3 - BORDER_WIDTH;
                        if(n == 1 || n == 4 || n == 7)
                            xpos = 0;
                        if(n == 2 || n == 5 || n == 8)
                            xpos = sw/3 + BORDER_WIDTH;
                        if(n == 3 || n == 6 || n == 9)
                            xpos = 2*(sw/3) + BORDER_WIDTH;
                        if(n == 4 || n == 7)
                            y += sh/3 + BORDER_WIDTH;
                        if(n == x && n == 7)
                            wdt = sw - BORDER_WIDTH;
                        if(n == x && n == 8)
                            wdt = 2*(sw/3) - BORDER_WIDTH;
                    } else
                        if(x >= 5) {
                            wdt = sw/3 - BORDER_WIDTH;
                            ht  = sh/2 - BORDER_WIDTH;
                            if(n == 1 || n == 4)
                                xpos = 0;
                            if(n == 2 || n == 5)
                                xpos = sw/3 + BORDER_WIDTH;
                            if(n == 3 || n == 6)
                                xpos = 2*(sw/3) + BORDER_WIDTH;
                            if(n == 4)
                                y += sh/2; // + BORDER_WIDTH;
                            if(n == x && n == 5)
                                wdt = 2*(sw/3) - BORDER_WIDTH;
                        } else {
                            if(x > 2) {
                                if(n == 1 || n == 2)
                                    ht = sh/2 + growth - BORDER_WIDTH;
                                if(n >= 3)
                                    ht = sh/2 - growth - 2*BORDER_WIDTH;
                            }
                            else
                                ht = sh - BORDER_WIDTH;
                            if(n == 1 || n == 3) {
                                xpos = 0;
                                wdt = master_size - BORDER_WIDTH;
                            }
                            if(n == 2 || n == 4) {
                                xpos = master_size+BORDER_WIDTH;
                                wdt = sw - master_size - 2*BORDER_WIDTH;
                            }
                            if(n == 3)
                                y += sh/2 + growth + BORDER_WIDTH;
                            if(n == x && n == 3)
                                wdt = sw - BORDER_WIDTH;
                        }
                    XMoveResizeWindow(dis, c->win, xpos, y, wdt, ht);
                }
            }
            break;
    }
    free(c);
}

void update_current(void) {
    client *c;

    for(c=head; c; c=c->next) {
        if(head->next == NULL || mode == MONOCYCLE)
            XSetWindowBorderWidth(dis, c->win, 0);
        else
            XSetWindowBorderWidth(dis, c->win, BORDER_WIDTH);

        if(current == c) { /* highlight current window */
            XSetWindowBorder(dis, c->win, win_focus);
            XSetInputFocus(dis, c->win, RevertToParent, CurrentTime);
            XRaiseWindow(dis, c->win);
            if(CLICK_TO_FOCUS == 0)
                XUngrabButton(dis, AnyButton, AnyModifier, c->win);
        } else {
            XSetWindowBorder(dis, c->win, win_unfocus);
            if(CLICK_TO_FOCUS == 0)
                XGrabButton(dis, AnyButton, AnyModifier, c->win, True, ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
        }
    }
    free(c);
    XSync(dis, False);
}

void switch_mode(const Arg arg) {
    if(mode == arg.i) return;
    mode = arg.i;
    if(mode == TILE || mode == GRID)
        master_size = sw * MASTER_SIZE;
    else if(mode == BSTACK)
        master_size = sh * MASTER_SIZE;
    tile();
    update_current();
}

void resize_master(const Arg arg) {
    master_size += arg.i;
    tile();
}

void resize_stack(const Arg arg) {
    growth += arg.i;
    tile();
}

/* ********************** Keyboard Management ********************** */
void grabkeys(void) {
    int i;
    KeyCode code;

    XUngrabKey(dis, AnyKey, AnyModifier, root);
    // For each shortcuts
    for(i=0; i<LENGTH(keys); i++) {
        code = XKeysymToKeycode(dis, keys[i].keysym);
        XGrabKey(dis, code, keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | LockMask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | numlockmask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | numlockmask | LockMask, root, True, GrabModeAsync, GrabModeAsync);
    }
}

void keypress(XEvent *e) {
    static unsigned int len = sizeof keys / sizeof keys[0];
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ev = &e->xkey;

    keysym = XKeycodeToKeysym(dis, (KeyCode)ev->keycode, 0);
    for(i = 0; i < len; i++) {
        if(keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)) {
            if(keys[i].function)
                keys[i].function(keys[i].arg);
        }
    }
}

void configurenotify(XEvent *e) {
    // Do nothing for the moment
}

/* ********************** Signal Management ************************** */
void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    wc.x = ev->x;
    wc.y = ev->y;
    if(ev->width < sw-BORDER_WIDTH)
        wc.width = ev->width;
    else
        wc.width = sw-BORDER_WIDTH;
    if(ev->height < sh-BORDER_WIDTH)
        wc.height = ev->height;
    else
        wc.height = sh-BORDER_WIDTH;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dis, ev->window, ev->value_mask, &wc);
    XSync(dis, False);
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;

    /* window is transient */
    Window trans;
    if(XGetTransientForHint(dis, ev->window, &trans) && trans) {
        add_window(ev->window);
        XMapWindow(dis, ev->window);
        XSetInputFocus(dis, ev->window, RevertToParent, CurrentTime);
        XRaiseWindow(dis, ev->window);
        return;
    }

    /* window type is not normal (_NET_WM_WINDOW_TYPE_NORMAL) */
    Atom realType, *wintype;
    int realFormat;
    unsigned long count, extra;
    unsigned char *data;
    if(XGetWindowProperty(dis, ev->window, atoms[_NET_WM_WINDOW_TYPE], 0, 32, False,
                 XA_ATOM, &realType, &realFormat, &count, &extra, &data) == Success)
        if(realType == XA_ATOM && count) {
            wintype = (unsigned long *)data;
            if(data) XFree(data);
            for(int i=0; i<count; i++)
                if(wintype[i] == atoms[_NET_WM_WINDOW_TYPE_UTILITY]      ||
                   wintype[i] == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION] ||
                   wintype[i] == atoms[_NET_WM_WINDOW_TYPE_SPLASH]       ||
                   wintype[i] == atoms[_NET_WM_WINDOW_TYPE_DIALOG]       ||
                   wintype[i] == atoms[_NET_WM_WINDOW_TYPE_DOCK]         ){
                    add_window(ev->window);
                    XMapWindow(dis, ev->window);
                    XSetInputFocus(dis, ev->window, RevertToParent, CurrentTime);
                    XRaiseWindow(dis, ev->window);
                    return;
                }
        }

    /* window is normal and has a rule set */
    XClassHint ch = {0};
    int cd = current_desktop;
    if(XGetClassHint(dis, ev->window, &ch))
        for(int i=0; i<nrules; i++)
            if(strcmp(ch.res_class, rules[i].class) == 0) {
                save_desktop(cd);
                select_desktop(rules[i].desktop);
                add_window(ev->window);
                select_desktop(cd);
                if(rules[i].follow)
                    change_desktop((Arg){.i = rules[i].desktop});
                if(ch.res_class)
                    XFree(ch.res_class);
                if(ch.res_name)
                    XFree(ch.res_name);
                return;
            }

    /* window is normal and has no rule set */
    add_window(ev->window);
    XMapWindow(dis, ev->window);
    tile();
    update_current();
}

void destroynotify(XEvent *e) {
    client *c;
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    int cd = current_desktop;
    Bool found = False;

    save_desktop(cd);
    for(int d=0; d<DESKTOPS && !found; select_desktop(d++))
        for(c=head; c; c=c->next)
            if((found = ev->window == c->win)) {
                removeclient(c);
                break;
            }
    select_desktop(cd);
}

void enternotify(XEvent *e) {
    client *c;
    XCrossingEvent *ev = &e->xcrossing;

    if(FOLLOW_MOUSE == 0) {
        if((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
            return;
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                current = c;
                update_current();
                return;
            }
    }
}

void buttonpressed(XEvent *e) {
    client *c;
    XButtonPressedEvent *ev = &e->xbutton;

    // change focus with LMB
    if(CLICK_TO_FOCUS == 0 && ev->window != current->win && ev->button == Button1)
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                current = c;
                update_current();
                return;
            }
}

void deletewindow(Window w) {
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = atoms[WM_PROTOCOLS];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = atoms[WM_DELETE_WINDOW];
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dis, w, False, NoEventMask, &ev);
}

unsigned long getcolor(const char* color) {
    Colormap map = DefaultColormap(dis,screen);
    XColor c;

    if(!XAllocNamedColor(dis,map,color,&c,&c))
        die("error: cannot allocate color '%s'\n", c);
    return c.pixel;
}

void quit(const Arg arg) {
    retval = arg.i;
    running = False;
}

void cleanup(void) {
    Window root_return;
    Window parent_return;
    Window *children;
    unsigned int nchildren;

    XUngrabKey(dis, AnyKey, AnyModifier, root);

    XQueryTree(dis, root, &root_return, &parent_return, &children, &nchildren);
    for(int i = 0; i < nchildren; i++)
        deletewindow(children[i]);
    free(children);

    XSync(dis, False);
    XSetInputFocus(dis, PointerRoot, RevertToPointerRoot, CurrentTime);
}

void setup(void) {
    sigchld(0);

    screen = DefaultScreen(dis);
    root = RootWindow(dis,screen);

    sw = XDisplayWidth(dis,screen)  - BORDER_WIDTH;
    sh = XDisplayHeight(dis,screen) - PANEL_HEIGHT - BORDER_WIDTH;

    master_size = ((mode == BSTACK) ? sh : sw) * MASTER_SIZE;
    for(int i=0; i < DESKTOPS; i++)
        save_desktop(i);
    change_desktop((Arg){.i = 0});

    win_focus = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);

    XModifierKeymap *modmap = XGetModifierMapping(dis);
    for (int k = 0; k < 8; k++) {
        for (int j = 0; j < modmap->max_keypermod; j++) {
            if(modmap->modifiermap[k * modmap->max_keypermod + j] == XKeysymToKeycode(dis, XK_Num_Lock))
                numlockmask = (1 << k);
        }
    }
    XFreeModifiermap(modmap);

    /* set up atoms for dialog/notification windows */
    atoms[WM_PROTOCOLS]     = XInternAtom(dis, "WM_PROTOCOLS",     False);
    atoms[WM_DELETE_WINDOW] = XInternAtom(dis, "WM_DELETE_WINDOW", False);
    atoms[_NET_WM_WINDOW_TYPE]         = XInternAtom(dis, "_NET_WM_WINDOW_TYPE",         False);
    atoms[_NET_WM_WINDOW_TYPE_UTILITY] = XInternAtom(dis, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    atoms[_NET_WM_WINDOW_TYPE_DOCK]    = XInternAtom(dis, "_NET_WM_WINDOW_TYPE_DOCK",    False);
    atoms[_NET_WM_WINDOW_TYPE_SPLASH]  = XInternAtom(dis, "_NET_WM_WINDOW_TYPE_SPLASH",  False);
    atoms[_NET_WM_WINDOW_TYPE_DIALOG]  = XInternAtom(dis, "_NET_WM_WINDOW_TYPE_DIALOG",  False);
    atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION] = XInternAtom(dis, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);

    /* check if another window manager is running */
    xerrorxlib = XSetErrorHandler(xerrorstart);
    XSelectInput(dis, DefaultRootWindow(dis), SubstructureNotifyMask|SubstructureRedirectMask);
    XSync(dis, False);
    XSetErrorHandler(xerror);
    XSync(dis, False);

    grabkeys();
}

int xerrorstart(Display *dis, XErrorEvent *ee) {
    die("error: another window manager is already running\n");
    return -1;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit.  */
int xerror(Display *dis, XErrorEvent *ee) {
    if(ee->error_code == BadWindow
            || (ee->error_code == BadMatch && (ee->request_code == X_SetInputFocus || ee->request_code ==  X_ConfigureWindow))
            || (ee->error_code == BadDrawable && (ee->request_code == X_PolyText8 || ee->request_code == X_PolyFillRectangle
                 || ee->request_code == X_PolySegment || ee->request_code == X_CopyArea))
            || (ee->error_code == BadAccess && ee->request_code == X_GrabKey))
        return 0;
    fprintf(stderr, "error: xerror: request code: %d, error code: %d\n", ee->request_code, ee->error_code);
    return xerrorxlib(dis, ee); /* may call exit */
}

void sigchld(int unused) {
    if(signal(SIGCHLD, sigchld) == SIG_ERR)
        die("error: can't install SIGCHLD handler\n");
    while(0 < waitpid(-1, NULL, WNOHANG));
}

void spawn(const Arg arg) {
    if(fork() == 0) {
        if(dis)
            close(ConnectionNumber(dis));
        setsid();
        execvp((char*)arg.com[0], (char**)arg.com);
        fprintf(stderr, "error: execvp %s", (char *)arg.com[0]);
        perror(" failed"); /* also prints the err msg */
        exit(EXIT_SUCCESS);
    }
}

void run(void) {
    XEvent ev;
    while(running && !XNextEvent(dis,&ev))
        if(events[ev.type])
            events[ev.type](&ev);
}

void die(const char *errstr, ...) {
    va_list ap;
    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if(argc == 2 && strcmp("-v", argv[1]) == 0) {
        fprintf(stdout, "dminiwm-%s\n", VERSION);
        return EXIT_SUCCESS;
    } else if(argc != 1)
        die("usage: dminiwm [-v]\n");
    if(!(dis = XOpenDisplay(NULL)))
        die("error: cannot open display\n");
    setup();
    run();
    cleanup();
    XCloseDisplay(dis);
    return retval;
}