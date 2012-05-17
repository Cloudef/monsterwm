#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <X11/extensions/Xcomposite.h>
#include "glcomposite.h"

static Display             *dis;
static Display             *gldis;
static int                 glscrn;
static Window              glroot;
static GLXContext          glctx;
static GLXFBConfig         pixconfig;
static int glwidth, glheight;

static bool glready = false;

/* GL extensions */
static PFNGLXBINDTEXIMAGEEXTPROC    glXBindTexImageEXT      = NULL;
static PFNGLXRELEASETEXIMAGEEXTPROC glXReleaseTexImageEXT   = NULL;

typedef struct glwin
{
   Window               win;
   unsigned int         tex;
   GLXPixmap            pix;
   int x, y, w, h;
   char hidden, mapped, alpha;
   struct glwin *prev, *next;
} glwin;

glwin *glstack = NULL;

void configuregl(XEvent *ev);
void destroygl(XEvent *ev);
void mapgl(XEvent *ev);
void unmapgl(XEvent *ev);
static void (*events[LASTEvent])(XEvent *e) = {
    [MapRequest] = mapgl, [DestroyNotify] = destroygl,
    [ConfigureRequest] = configuregl,
};

/* bind windows to texture */
static void bind_glwin(glwin *win)
{
   glBindTexture(GL_TEXTURE_2D, win->tex);

   /* set filtering */
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   /* bind pixmap */
   glXBindTexImageEXT(gldis, win->pix, GLX_FRONT_LEFT_EXT, NULL);

   /* start draw */
   XGrabServer(dis); /* tearless */
   glXWaitX();
}

/* unbinds window from texture */
static void unbind_glwin(glwin *win)
{
   /* stop draw */
   glXReleaseTexImageEXT(gldis, win->pix, GLX_FRONT_LEFT_EXT);
   XUngrabServer(dis);

   /* unbind texture */
   glBindTexture(GL_TEXTURE_2D, 0);
}

/* creates gl texture from X pixmap */
static int pixmap_glwin(glwin *win)
{
   int pixatt[] = { GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
                    GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGB_EXT,
                    None };
   Pixmap pixmap;

   /* destory old pixmap if it exists */
   if (win->pix) glXDestroyPixmap(gldis, win->pix);

   /* create gl texture from pixmap */
   pixmap = XCompositeNameWindowPixmap(dis, win->win);

   win->pix = glXCreatePixmap(gldis, pixconfig, pixmap, pixatt);
   XFreePixmap(dis, pixmap);

   if (!win->tex) glGenTextures(1, &win->tex);
   return 1;
}

/* updates window position */
static void update_glwin(glwin *win)
{
   Window root;
   int x, y;
   unsigned int w, h, bw, d;
   assert(win);

   if (!(XGetGeometry(dis, win->win,
               &root, &x, &y, &w, &h, &bw, &d)))
      return;

   win->w = w; win->h = h,
   win->x = x; win->y = y;

   /* mark this window as hidden? */
   win->hidden = 0;
        if (win->x + win->w < 1 || win->x > glwidth)  win->hidden = 1;
   else if (win->y + win->h < 1 || win->y > glheight) win->hidden = 1;
}

/* allocate gl window */
static glwin* alloc_glwin(Window win, glwin *prev)
{
   glwin *w;
   XWindowAttributes wa;

   if (!(XGetWindowAttributes(dis, win, &wa)) || wa.override_redirect)
      return NULL;

   /* allocate to stack */
   if (!(w = malloc(sizeof(glwin)))) return NULL;
   memset(w, 0, sizeof(glwin));
   w->win      = win;
   w->prev     = prev;

#if 0
   /* create damage for window */
   w->dam = xcb_generate_id(dis);
   xcb_damage_create(dis, w->dam, win, XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);
#endif

   /* update window information first */
   update_glwin(w);

   /* redirect window */
   XCompositeRedirectWindow(dis, win, CompositeRedirectManual);

   /* create pixmap */
   if (!pixmap_glwin(w)) {
      free(w);
      return NULL;
   }

   return w;
}

/* free glwin pointer and correct stack */
static glwin* dealloc_glwin(glwin *w)
{
   glwin *prev;
   assert(w);

   /* point stack to right windows */
   if (!(prev = w->prev)) glstack = NULL;
   else w->prev->next = w->next;

   /* release */
   // if (w->dam) xcb_damage_destroy(dis, w->dam);

   /* free */
   if (w->pix) glXDestroyPixmap(gldis, w->pix);
   if (w->tex) glDeleteTextures(1, &w->tex);
   free(w);
   return prev;
}

/* point X window to glwindow */
static glwin* win_to_glwin(Window win)
{
   for (glwin *w = glstack; w; w = w->next)
      if (w->win == win) return w;
   return NULL;
}

/* add new glwindow to stack */
static glwin* add_glwin(Window win)
{
   glwin *w;
   if ((w = win_to_glwin(win))) return w; /* already exists */
   if (!glstack)                return (glstack = alloc_glwin(win, NULL));
   for (w = glstack; w && w->next;  w = w->next);
   return (w->next = alloc_glwin(win, w));
}

/* raises glwin to top of stack */
static void raise_glwin(glwin *win)
{
   glwin *w;
   assert(win);
   if (win->prev) win->prev->next = win->next;
   else           glstack = win->next;
   for (w = glstack; w && w->next; w = w->next);
   w->next = win;
}

/* raise glwindow using X window as argument */
void raisegl(Window win)
{
   raise_glwin(win_to_glwin(win));
}

/* draw glwindow */
static void draw_glwin(glwin *win)
{
   float wx, wy, ww, wh;
   assert(win);

   /* not mapped */
   if (!win->mapped) return;

   update_glwin(win);
   pixmap_glwin(win);

   /* no need to draw */
   if (win->hidden) return;

   ww  = (float)win->w/glwidth; wh = (float)win->h/glheight;
   wx  = (float)win->x/glwidth; wy = (float)(glheight-win->y)/glheight;
   wy -= 1; /* The above coords are for _center of the window_, - with 1 and we get top of the window :) */

   /* TODO: use modern drawing methods, FBO's and such */
   bind_glwin(win);
   glBegin(GL_TRIANGLE_STRIP);
   glTexCoord2f(1, 0); glVertex3f(wx+ww, wy+wh, 0);
   glTexCoord2f(0, 0); glVertex3f(wx-ww, wy+wh, 0);
   glTexCoord2f(1, 1); glVertex3f(wx+ww, wy-wh, 0);
   glTexCoord2f(0, 1); glVertex3f(wx-ww, wy-wh, 0);
   glEnd();
   unbind_glwin(win);
}

/* print a message on standard error stream
 * and exit with failure exit code
 */
static void die(const char *errstr, ...) {
   va_list ap;
   va_start(ap, errstr);
   vfprintf(stderr, errstr, ap);
   va_end(ap);
   exit(EXIT_FAILURE);
}

/* choose pixmap framebuffer configuration */
static GLXFBConfig ChoosePixmapFBConfig()
{
   GLXFBConfig *confs;
   int i, nconfs, value;

   confs = glXGetFBConfigs(gldis, glscrn, &nconfs);
   for (i = 0; i != nconfs; i++) {
      glXGetFBConfigAttrib(gldis, confs[i], GLX_DRAWABLE_TYPE, &value);
      if (!(value & GLX_PIXMAP_BIT))
         continue;

      glXGetFBConfigAttrib(gldis, confs[i], GLX_BIND_TO_TEXTURE_TARGETS_EXT, &value);
      if (!(value & GLX_TEXTURE_2D_BIT_EXT))
         continue;

      glXGetFBConfigAttrib(gldis, confs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &value);
      if (value == False) {
         glXGetFBConfigAttrib(gldis, confs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &value);
         if (value == False)
            continue;
      }

      glXGetFBConfigAttrib(gldis, confs[i], GLX_Y_INVERTED_EXT, &value);
      /* if value == TRUE, invert */

      break;
   }

   return confs[i];
}

/* setup GL window */
int setupgl(Window  root, int width, int height)
{
   GLint att[] = { GLX_RGBA, GLX_DOUBLEBUFFER, None };
   XVisualInfo *vi;
   GLXContext  glctx;
   const char *extensions;

   glroot = root;
   if (!(vi = glXChooseVisual(gldis, 0, att))) {
      puts("glXChooseVisual failed.");
      return 0;
   }
   glctx = glXCreateContext(gldis, vi, NULL, GL_TRUE);
   if (!glXMakeCurrent(gldis, glroot, glctx)) {
      puts("glXMakeCurrent failed.");
      return 0;
   }
   glViewport(0,0,(glwidth = width), (glheight = height));

   extensions = glXQueryExtensionsString(gldis, glscrn);
   if (!strstr(extensions, "GLX_EXT_texture_from_pixmap")) {
      puts("GLX_EXT_texture_from_pixmap extension is not supported on your driver.");
      return 0;
   }

   glXBindTexImageEXT    = (PFNGLXBINDTEXIMAGEEXTPROC)    glXGetProcAddress((GLubyte*) "glXBindTexImageEXT");
   glXReleaseTexImageEXT = (PFNGLXRELEASETEXIMAGEEXTPROC) glXGetProcAddress((GLubyte*) "glXReleaseTexImageEXT");

   if (!glXBindTexImageEXT || !glXReleaseTexImageEXT)
   {
      puts("glXGetProcAddress failed.");
      return 0;
   }

   /* redirect all windows */
   // XCompositeRedirectSubWindows(dis, glroot, CompositeRedirectManual);

   /* get framebuffer configuration for pixmaps */
   pixconfig = ChoosePixmapFBConfig(gldis);
   glEnable(GL_TEXTURE_2D);
   glEnable(GL_BLEND);
   glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   /* setup bg color */
   glClearColor(.0, .0, .0, 1.0);

   return 1;
}

/* swapbuffers */
void swapgl()
{
   glXSwapBuffers(gldis, glroot);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void destroygl(XEvent *e)
{
   glwin *win;
   XDestroyWindowEvent *ev = &e->xdestroywindow;
   if ((win = win_to_glwin(ev->window)))
      dealloc_glwin(win);
}

void unmapgl(XEvent *e)
{
   glwin *win;
   XUnmapEvent *ev = &e->xunmap;
   if (!(win = win_to_glwin(ev->window)))
      return;
   win->mapped = 0;
}

void mapgl(XEvent *e)
{
   glwin *win;
   XMapRequestEvent *ev = &e->xmaprequest;
   if (!(win = add_glwin(ev->window)))
      return;
   win->mapped = 1;
}

void configuregl(XEvent *e)
{
   XConfigureRequestEvent *ev = &e->xconfigurerequest;
}

void eventgl(XEvent *ev)
{
   if (events[ev->type]) events[ev->type](ev);
}

/* temporary loop code */
void loopgl()
{
   glwin *win;

   glready = true;
   if (!glready) return;
   for (win = glstack; win; win = win->next)
      draw_glwin(win);
   glready = false;
}

/* open openGL connection which needs x11-xcb */
int connectiongl(Display *wmdis, int *screen)
{
   int major, minor;
   dis = gldis = wmdis;
   glscrn = DefaultScreen(gldis);

   if (!XCompositeQueryVersion(dis, &major, &minor))
      die("error: could not query composite extension version\n");

   if (minor < 2)
      die("error: composite extension 0.2 or newer needed!\n");

#if 0
   xcb_prefetch_extension_data(dis, &xcb_damage_id);
   if (!xcb_get_extension_data(dis, &xcb_damage_id))
      die("error: no damage extension\n");
#endif

   if (screen) *screen = glscrn;
   return 1;
}

/* close openGL connection */
void closeconnectiongl()
{
   glwin *wn;

   /* free all windows */
   for (glwin *w = glstack; w; w = wn) wn = dealloc_glwin(w);

   glXDestroyContext(gldis, glctx);
}
