#include <GL/glx.h>
#include <GL/gl.h>

int setupgl(Window root, int width, int height);
void raisegl(Window win);
void eventgl(XEvent *ev);
void loopgl();
void swapgl();
int connectiongl();
void closeconnectiongl();
