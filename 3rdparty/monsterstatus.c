#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <alsa/asoundlib.h>
#include <mpd/client.h>

#define aligncenter() printf("\\c")
#define alignleft()   printf("\\l")
#define alignright()  printf("\\r")
#define whitespace()  printf(" \\f2| ")
#define eol()         printf("\n")

#define DEFAULT      1
#define LAYOUT       8

#define VOL_FG          5

#define DATE_SEP        "\\f9>\\f3>"
#define DATE_FG         7
#define TIME_FG         7

#define UPDATE_FG       2
#define COWER_FG        3

#define MPD_TIME        "\\f3%d:%.2d \\f1/ \\f3%d:%.2d\\f5"
#define MPD_SEP         "\\f9>\\f3>"
#define MPD_ARTIST_FG   7
#define MPD_ALBUM_FG    7
#define MPD_TITLE_FG    7

#define BAT_ROOT     "/sys/class/power_supply/bq27500-0"
#define BAT_CHARGE   "capacity"
#define BAT_STATE    "status"

#define BAT_CHARGING 1
#define BAT_GOOD     1
#define BAT_AVEG     1
#define BAT_BAD      1

#define batcmp(x,y) memcmp(x, y, strlen(y))

#define SLEEP_INTERVAL  1000 /* ms */
#define MPD_TIMEOUT     3000

typedef struct desktop_t {
   int current, urgent, windows;
   const char *n;
   char c;
} desktop_t;

typedef struct layout_t {
   const char *n;
   char c;
} layout_t;

static desktop_t desktop[] = {
   { .n = "WEB", .c = DEFAULT },
   { .n = "DEV", .c = DEFAULT },
   { .n = "FOO", .c = DEFAULT },
   { .n = "MPV", .c = DEFAULT },
   { .n = NULL }
};

static layout_t layout[]  = {
   { .n = "CLASSIC", .c = LAYOUT },
   { .n = "MONOCLE", .c = LAYOUT },
   { .n = "BACKSTAB", .c = LAYOUT },
   { .n = "GRID", .c = LAYOUT },
   { .n = "TROLL", .c = LAYOUT },
   { .n =  NULL }
};

enum {
   PLAY_REPEAT  = 0x1,
   PLAY_RANDOM  = 0x2,
   PLAY_SINGLE  = 0x4,
   PLAY_CONSUME = 0x8
};

/* mpd state definition */
typedef struct mpdstate {
   unsigned int id;
   unsigned int queuever;
   unsigned int queuelen;
   unsigned int crossfade;
   unsigned int playmode;
   unsigned int duration;
   unsigned int elapsed;
   int volume;
   int song;
   enum mpd_state state;
} mpdstate;

/* mpd client definition */
typedef struct mpdclient {
   mpdstate state;
   struct mpd_connection *connection;
   struct mpd_status *status;
} mpdclient;
static mpdclient *mpd = NULL;

static void die(const char *errstr, ...) {
   va_list ap;
   va_start(ap, errstr); vfprintf(stderr, errstr, ap); va_end(ap);
   exit(EXIT_FAILURE);
}

static int execget(char *bin, char *buffer, size_t len) {
   FILE *p;
   size_t read;

   memset(buffer, 0, len);
   if (!(p = popen(bin, "r")))
      return 0;

   read = fread(buffer, 1, len-1, p);
   fclose(p);

   if (read && buffer[read-1] == '\n')
      buffer[read-1] = 0;
   return read;
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
   if ((*dst)[0]) free((*dst)[0]);
   free((*dst));
}

static void mpd_quit(void) {
   assert(mpd);
   if (mpd->connection) mpd_connection_free(mpd->connection);
   if (mpd->status)     mpd_status_free(mpd->status);
   free(mpd); mpd = NULL;
}

static int mpd_init(void) {
   unsigned int mpd_port = 6600;
   const char *host = getenv("MPD_HOST");
   const char *port = getenv("MPD_PORT");
   const char *pass = getenv("MPD_PASSWORD");

   if (!host)  host     = "localhost";
   if (port)   mpd_port = strtol(port, (char**) NULL, 10);

   if (mpd) mpd_quit();
   if (!(mpd = malloc(sizeof(mpdclient))))
      die("mpdclient allocation failed");
   memset(mpd, 0, sizeof(mpdclient));

   if (!(mpd->connection = mpd_connection_new(host, mpd_port, MPD_TIMEOUT)) ||
         mpd_connection_get_error(mpd->connection))
      return 0;

   if (pass && !mpd_run_password(mpd->connection, pass))
      return 0;

   return 1;
}

static void mpd_now_playing(void) {
   char *basec = NULL, *based = NULL;
   struct mpd_song *song = mpd_run_current_song(mpd->connection);
   if (!song) return;
   const char *disc    = mpd_song_get_tag(song, MPD_TAG_DISC, 0);
   const char *track   = mpd_song_get_tag(song, MPD_TAG_TRACK, 0);
   const char *comment = mpd_song_get_tag(song, MPD_TAG_COMMENT, 0);
   const char *genre   = mpd_song_get_tag(song, MPD_TAG_GENRE, 0);
   const char *album   = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
   const char *title   = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
   const char *artist  = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
   if (!title)  title  = mpd_song_get_tag(song, MPD_TAG_NAME, 0);
   if (!artist) artist = mpd_song_get_tag(song, MPD_TAG_ALBUM_ARTIST, 0);
   if (!artist) artist = mpd_song_get_tag(song, MPD_TAG_COMPOSER, 0);
   if (!artist) artist = mpd_song_get_tag(song, MPD_TAG_PERFORMER, 0);
   if (!album && (based = strdup(mpd_song_get_uri(song)))) album = basename(dirname(based));
   if (!title && (basec = strdup(mpd_song_get_uri(song)))) title = basename(basec);

   /* fallbacks */
   if (!artist) artist = "noartist";
   if (!album)  album  = "noalbum";
   if (!title)  title  = "notitle";

   int em, es, dm, ds;
   em = mpd->state.elapsed / 60;
   es = mpd->state.elapsed - em * 60;
   dm = mpd->state.duration / 60;
   ds = mpd->state.duration - dm * 60;

   if (artist && album && title)  {
      printf(MPD_TIME" "MPD_SEP" "
             "\\f%d%s "MPD_SEP" \\f%d%s "MPD_SEP" \\f%d%s\\f1", em, es, dm, ds,
            MPD_ARTIST_FG, artist, MPD_ALBUM_FG, album, MPD_TITLE_FG, title);
   }
   mpd_song_free(song);

   if (based) free(based);
   if (basec) free(basec);
}

static int mpd_update_status(void) {
   if (mpd->status)    mpd_status_free(mpd->status);
   if (!(mpd->status = mpd_run_status(mpd->connection)))
      die("mpd_run_status failed");

   mpd->state.id        = mpd_status_get_update_id(mpd->status);
   mpd->state.volume    = mpd_status_get_volume(mpd->status);
   mpd->state.crossfade = mpd_status_get_crossfade(mpd->status);
   mpd->state.queuever  = mpd_status_get_queue_version(mpd->status);
   mpd->state.queuelen  = mpd_status_get_queue_length(mpd->status);
   mpd->state.song      = mpd_status_get_song_id(mpd->status);
   mpd->state.elapsed   = mpd_status_get_elapsed_time(mpd->status);
   mpd->state.duration  = mpd_status_get_total_time(mpd->status);
   mpd->state.state     = mpd_status_get_state(mpd->status);

   if (mpd_status_get_repeat(mpd->status))
      mpd->state.playmode |= PLAY_REPEAT;
   if (mpd_status_get_random(mpd->status))
      mpd->state.playmode |= PLAY_RANDOM;
   if (mpd_status_get_single(mpd->status))
      mpd->state.playmode |= PLAY_SINGLE;
   if (mpd_status_get_consume(mpd->status))
      mpd->state.playmode |= PLAY_CONSUME;

   return mpd->state.song;
}

static snd_mixer_t* alsainit(const char *card)
{
   snd_mixer_t *handle;
   snd_mixer_open(&handle, 0);
   snd_mixer_attach(handle, card);
   snd_mixer_selem_register(handle, NULL, NULL);
   snd_mixer_load(handle);
   return handle;
}

static void alsaclose(snd_mixer_t *handle)
{
   snd_mixer_close(handle);
}

static snd_mixer_elem_t* alsamixer(snd_mixer_t *handle, const char *mixer)
{
   snd_mixer_selem_id_t *sid;
   snd_mixer_selem_id_alloca(&sid);
   snd_mixer_selem_id_set_index(sid, 0);
   snd_mixer_selem_id_set_name(sid, mixer);
   return snd_mixer_find_selem(handle, sid);
}

int getvolume(snd_mixer_elem_t *mixer)
{
   long volume, min, max;
   int mute;
   snd_mixer_selem_get_playback_volume_range(mixer, &min, &max);
   snd_mixer_selem_get_playback_volume(mixer, SND_MIXER_SCHN_MONO, &volume);
   snd_mixer_selem_get_playback_switch(mixer, SND_MIXER_SCHN_MONO, &mute);
   return !mute?0:volume>min?volume<max?(volume*100)/max:100:0;
}

static int batterystate(char *buffer)
{
   FILE *f;
   if (!(f = fopen(BAT_ROOT"/"BAT_STATE, "r")))
      return 0;
   fgets(buffer, 12, f); fclose(f);
   return 1;
}

static int batterycharge(int *charge)
{
   FILE *f; char buffer[5]; *charge = 0;
   if (!(f = fopen(BAT_ROOT"/"BAT_CHARGE, "r")))
      return 0;
   fgets(buffer, 4, f);
   *charge = strtol(buffer, (char **) NULL, 10);
   fclose(f);
   return 1;
}

static int printmpd() {
   if (mpd_init() && mpd_update_status() != -1) {
      mpd_now_playing();
      mpd_quit();
      return 1;
   }
   return 0;
}

static void printpacman(int update) {
   static char buffer[5];
   static int times = 1;

   if (!update && --times==0) {
      execget("pacup -u 2>/dev/null | wc -l", buffer, 5);
      times = 15;
   }

   printf("\\f%d%s\\f1", UPDATE_FG, buffer);
}

static void printcower(int update) {
   static char buffer[5];
   static int times = 1;

   if (!update && --times==0) {
      execget("cower -u 2>/dev/null | wc -l", buffer, 5);
      times = 15;
   }

   printf("\\f%d%s\\f1", COWER_FG, buffer);
}

static void printvolume(snd_mixer_elem_t *mixer) {
   int volume;
   volume = getvolume(mixer);
   printf("\\f%d%3d%%\\f1", VOL_FG, volume);
}

static void printbattery() {
   char state[13]; int charge, color;
   if (!batterystate(state) || !batterycharge(&charge)) {
      printf("FAIL");
      return;
   }
   if (!batcmp(state, "Charging"))  color = BAT_CHARGING;
   else if (charge > 65)            color = BAT_GOOD;
   else if (charge > 25)            color = BAT_AVEG;
   else                             color = BAT_BAD;
   printf("\\f%d%s %3d%%", color, !batcmp(state, "Charging")?"CHR":"BAT", charge);
}

static void printdate() {
   char date[12], clock[8];
   time_t rawtime; struct tm *timeinfo;
   time(&rawtime);
   timeinfo = localtime(&rawtime);
   strftime(date, sizeof(date)-1, "%a %d/%m", timeinfo);
   strftime(clock, sizeof(clock)-1, "%H:%M", timeinfo);
   printf("\\f%d%s "DATE_SEP" \\f%d%s\\f1", DATE_FG, date, TIME_FG, clock);
}

static void printdata()
{
   int i;
   char color;

   for (i = 0; desktop[i].n; ++i) {
      color = desktop[i].c;
      if (desktop[i].current)
        printf("\\f9\\u9\\b0 ");
      else if (desktop[i].urgent)
        printf("\\f9\\u3\\b0 ");
      else printf("\\f9\\u0\\b0 ");

      printf("%s", desktop[i].n);
      printf(" \\u0\\b0");
   }
}

static void printlayout(int mode)
{
   printf("\\f%d%s MODE\\f1", LAYOUT, layout[mode].n);
}

static void monsterpager(const char *fifo, int fd, int monitor, int desks, int *mode)
{
   size_t bytes = 0;
   int tags = 0, i = 0, co = 0;
   int mon = 0, amon = 0, d = 0, w = 0, m = 0, c = 0, u = 0;
   char buffer[256], **data = NULL;

   memset(buffer, 0, sizeof(buffer));
   while ((bytes = read(fd, buffer, sizeof(buffer)-1)) >= 0)
   {
      /* check if eof */
      if (!bytes) {
         close(fd);
         if ((fd = open(fifo, O_RDONLY)) == -1)
            die("could not reopen fifo\n");
         break;
      }

      /* not right kind of data */
      if (buffer[1] != ':' || buffer[3] != ':' ||
          buffer[5] != ':' || buffer[7] != ':') {
         memset(buffer, 0, bytes);
         printdata();
         break;
      }

      /* strsplit */
      tags = strsplit(&data, buffer, " ");
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
            *mode = m;
         } else   desktop[d].current = 0;
         desktop[d].windows = w;
      }

      printdata();
      strsplit_clear(&data);
      break;
   }
}

int main(int argc, char **argv)
{
   FILE *f;
   int afds = 1;
   fd_set rfds;
   struct timeval tv;
   struct pollfd fd2[afds];
   int fd, lfd, rc = 0;
   int desks = 0, layouts = 0, mode = 0;
   int monitor = 0, i = 0, cycles = 0;
   char update = 0;
   snd_mixer_t *alsa; snd_mixer_elem_t *mixer;
   memset(&fd2, 0, sizeof(fd2));

   /* check args */
   if (argc < 2)
      die("usage: %s <fifo> [monitor]\n", argv[0]);

   /* init alsa */
   if (!(alsa = alsainit("default")))
      die("Could not init ALSA (%s)\n", "default");
   /* get mixer */
   if (!(mixer = alsamixer(alsa, "Master")))
      die("Could not get mixer (%s)\n", "Master");
   /* get fds for select */
   snd_mixer_poll_descriptors(alsa, fd2, sizeof(fd2));

   if ((f = fopen(argv[1], "r")))
      fclose(f);
   else {
      if (mkfifo(argv[1], 0600) == -1)
         die("could not  create fifo\n");
   }

   /* open fifo */
   if ((fd = open(argv[1], O_RDONLY|O_NONBLOCK)) == -1)
      die("could not open fifo\n");

   /* assign monitor */
   if (argc > 2)
      monitor = strtol(argv[2], (char **) NULL, 10);

   /* count layouts && desks */
   for (desks = 0; desktop[desks].n;  ++desks) {
      desktop[desks].current = 0;
      desktop[desks].urgent  = 0;
      desktop[desks].windows = 0;
   }
   for (layouts = 0; layout[layouts].n; ++layouts);

   /* init */
   FD_ZERO(&rfds);

   /* statusbar itself (pipe to lemonbar) */
   while (1) {
      /* left */
      alignleft();
      //printf(" \\f2|");
      printf(" ");
      if (FD_ISSET(fd, &rfds)) {
         monsterpager(argv[1], fd, monitor, desks, &mode);
      } else {
         printdata();
      }
      printf("\\f2| ");
      printlayout(mode<layouts?mode:0);

      /* center */
      aligncenter();

      /* right */
      alignright();
      if (printmpd()) whitespace();
      printcower(update);
      whitespace();
      printpacman(update);
      whitespace();
      printvolume(mixer);
#if 0  /* PANDORA */
      whitespace();
      printbattery();
#endif
      whitespace();
      printdate();
      printf(" ");
      eol();
      fflush(stdout);

      FD_ZERO(&rfds);
      FD_SET((lfd = fd), &rfds);
      for (i = 0; i != afds; ++i)
         if (fd2[i].fd) {
            if (fd2[i].fd > lfd) lfd = fd2[i].fd;
            FD_SET(fd2[i].fd, &rfds);
         }
      tv.tv_sec = SLEEP_INTERVAL/1000;
      tv.tv_usec = 0;
      select(lfd + 1, &rfds, NULL, NULL, &tv);

      if (!(update = FD_ISSET(fd, &rfds))){
         for (i = 0; i != afds; ++i)
            if (fd2[i].fd && FD_ISSET(fd2[i].fd, &rfds)) {
               update = 1;
               break;
            }
      }
      if (!update) cycles = 0;
      else if (++cycles > 1) usleep(5000);
      snd_mixer_handle_events(alsa);
   }

   /* close alsa */
   alsaclose(alsa);
   close(fd);
   unlink(argv[1]);
   return EXIT_SUCCESS;
}
