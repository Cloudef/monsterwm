#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* either color code or empty for default fg */
#define URGENT       "#B7CE42"
#define CURRENT      "#D81860"
#define DEFAULT      "#5E7175"
#define LAYOUT       "#FEA63C"
#define SEPERATOR    "::"
#define SEPERATOR_FG ""
#define WINDOWS      LAYOUT
#define VIMODE       CURRENT

#define eol()        printf("\n");
#define whitespace() printf(" ");

typedef struct desktop_t
{
   const char *n, *c;
   int current, urgent, windows;
} desktop_t;

typedef struct layout_t
{
   const char *n, *c;
} layout_t;

static desktop_t desktop[] = {
   { .n = "web", .c = DEFAULT },
   { .n = "dev", .c = DEFAULT },
   { .n = "foo", .c = DEFAULT },
   { .n = "mplayer", .c = DEFAULT },
   { .n = NULL }
};

static layout_t layout[]  = {
   { .n = "[T]", .c = LAYOUT },
   { .n = "[M]", .c = LAYOUT },
   { .n = "[B]", .c = LAYOUT },
   { .n = "[G]", .c = LAYOUT },
   { .n = "[F]", .c = LAYOUT },
   { .n =  NULL }
};

static void die(const char *errstr, ...) {
   va_list ap;
   va_start(ap, errstr); vfprintf(stderr, errstr, ap); va_end(ap);
   exit(EXIT_FAILURE);
}

static int strsplit(char ***dst, char *str, char *token) {
   char *saveptr, *ptr, *start;
   int32_t t_len, i;

   if (!(saveptr=strdup(str)))
      return 0;

   *dst=NULL;
   t_len=strlen(token);
   i=0;

   for (start=saveptr,ptr=start;;ptr++) {
      if (!strncmp(ptr,token,t_len) || !*ptr) {
         while (!strncmp(ptr,token,t_len)) {
            *ptr=0;
            ptr+=t_len;
         }

         if (!((*dst)=realloc(*dst,(i+2)*sizeof(char*))))
            return 0;
         (*dst)[i]=start;
         (*dst)[i+1]=NULL;
         i++;

         if (!*ptr)
            break;
         start=ptr;
      }
   }
   return i;
}

static void strsplit_clear(char ***dst) {
   if ((*dst)[0])
      free((*dst)[0]);
   free((*dst));
}

static void printdata(int m)
{
   int i;
   char *color;

   /* print desktops */
   for (i = 0; desktop[i].n; ++i) {
      color = (char*)desktop[i].c;
      if (desktop[i].current)       color = CURRENT;
      else if (desktop[i].urgent)   color = URGENT;
      printf("^fg(%s)%s%s%.0d%s%s^fg()",
            color, desktop[i].n, desktop[i].windows?" ^fg("WINDOWS")[":"", desktop[i].windows, desktop[i].windows?"]":"",
            desktop[i+1].n?"^fg("SEPERATOR_FG") "SEPERATOR" ":" ");
   }
   printf("^fg(%s)%s^fg() ", LAYOUT, layout[m].n);
   eol();
   fflush(stdout);
}

int main(int argc, char **argv)
{
   FILE *f;
   size_t bytes;
   int fd, tags = 0, i = 0, co = 0;
   int mon = 0, amon = 0, d = 0, w = 0, m = 0, c = 0, u = 0;
   int desks = 0, layouts = 0, mode = 0;
   int monitor = 0;
   char buffer[256], **data = NULL;


   /* check args */
   if (argc < 2)
      die("usage: %s <fifo> [monitor]\n", argv[0]);

#if 0
   /* remove if old fifo */
   if ((f = fopen(argv[1], "r"))) {
      fclose(f);
      unlink(argv[1]);
   }

   /* make fifo */
   if (mkfifo(argv[1], 0600) == -1)
      die("could not create fifo\n");
#else
   if ((f = fopen(argv[1], "r")))
       fclose(f);
    else {
       if (mkfifo(argv[1], 0600) == -1)
           die("could not  create fifo\n");
    }
#endif

   /* open fifo */
   if ((fd = open(argv[1], O_RDONLY)) == -1)
      die("could not open fifo\n");

   /* assign monitor */
   if (argc > 2)
       monitor = strtol(argv[2], (char **) NULL, 10);

   /* count layouts && desks */
   for (desks   = 0; desktop[desks].n;  ++desks) {
      desktop[desks].current = 0;
      desktop[desks].urgent  = 0;
      desktop[desks].windows = 0;
   }
   for (layouts = 0; layout[layouts].n; ++layouts);
   memset(buffer, 0, sizeof(buffer));

   /* tagbar itself (pipe to dzen) */
   while ((bytes = read(fd, buffer, sizeof(buffer))) >= 0)
   {
      /* check if eof */
      if (!bytes) {
         close(fd);
         if ((fd = open(argv[1], O_RDONLY)) == -1)
            die("could not reopen fifo\n");
         continue;
      }

      /* not right kind of data */
      if (buffer[1] != ':' || buffer[3] != ':' ||
          buffer[5] != ':' || buffer[7] != ':') {
         memset(buffer, 0, bytes);
         printdata(mode<layouts?mode:0);
         continue;
      }

      /* strsplit */
      tags = strsplit(&data, buffer, " ");
      whitespace();
      for(i = 0; i != tags; ++i) {
         /* sscanf data */
         co = sscanf(data[i], "%d:%d:%d:%d:%d:%d:%d",
               &mon, &amon, &d, &w, &m, &c, &u);
         if (mon != monitor) continue;
         if (co < 7)      break;
         if (d  >= desks) break;

         /* desktop flags */
         if (u) desktop[d].urgent = 1;
         else   desktop[d].urgent = 0;
         if (c) {
            desktop[d].current = 1;
            mode = m;
         } else   desktop[d].current = 0;
         desktop[d].windows = w;
      }

      printdata(mode<layouts?mode:0);
      strsplit_clear(&data);
      memset(buffer, 0, bytes);
   }

   /* close */
   close(fd);
   unlink(argv[1]);
   return EXIT_SUCCESS;
}
