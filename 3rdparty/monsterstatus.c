#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>
#include <alsa/asoundlib.h>
#include <mpd/client.h>

#define whitespace() printf("|")
#define eol()        printf("\n")

#define DEF_FG          "#CACACA"
#define DEF_BG          "#121212"

#define VOL_FG          "#AFD700"
#define VOL_BG          "#363636"
#define VOL_PERC_FG     "#DDEEDD"
#define VOL_PERC_BG     DEF_BG

#define DATE_FG         DEF_FG
#define DATE_BG         "#282828"
#define TIME_FG         "#66AABB"
#define TIME_BG         DEF_BG

#define UPDATE_FG       "#FEA63C"
#define UPDATE_BG       DEF_BG

#define COWER_FG        VOL_FG
#define COWER_BG        DEF_BG

#define MPD_FONT        "Kochi Gothic"
#define MPD_ARTIST_FG   "#888888"
#define MPD_ALBUM_FG    "#999999"
#define MPD_TITLE_FG    "#DDDDDD"
#define MPD_ARTIST_BG   "#000000"
#define MPD_ALBUM_BG    "#222222"
#define MPD_TITLE_BG    "#333333"

#define BAT_ROOT     "/sys/class/power_supply/bq27500-0"
#define BAT_CHARGE   "capacity"
#define BAT_STATE    "status"

#define BAT_CHARGING "#FEA63C"
#define BAT_GOOD     VOL_FG
#define BAT_AVEG     TIME_FG
#define BAT_BAD      "#FF6600"
#define BAT_BG       VOL_BG

#define batcmp(x,y) memcmp(x, y, strlen(y))

#define SLEEP_INTERVAL  1000 /* ms */
#define MPD_TIMEOUT     3000

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

static int execget(char *bin, char *buffer, size_t len)
{
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
   char *basec;
   struct mpd_song *song = mpd_run_current_song(mpd->connection);
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
   if (!album && (basec = strdup(mpd_song_get_uri(song)))) {
      album = basename(basec); free(basec);
   }

   int em, es, dm, ds;
   em = mpd->state.elapsed / 60;
   es = mpd->state.elapsed - em * 60;
   dm = mpd->state.duration / 60;
   ds = mpd->state.duration - dm * 60;

   if (artist && album && title)
      printf("|^fg(%s)%d:%.2d/^fg(%s)%d:%.2d|"
            "^fn("MPD_FONT")"
             "^bg(%s)^fg(%s)%s^bg(%s)^fg(%s)%s^bg(%s)^fg(%s)%s^bg()^fg()"
             "^fn()",
            TIME_FG, em, es, DATE_FG, dm, ds,
            MPD_ARTIST_BG, MPD_ARTIST_FG, artist,
            MPD_ALBUM_BG,  MPD_ALBUM_FG,  album,
            MPD_TITLE_BG,  MPD_TITLE_FG,  title);
   mpd_song_free(song);
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
   int percent, mute;
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

static void gdbar(char *buffer, int perc, char *fg, char *bg, int w, int h)
{
   int fill, unfill;
   fill   = (perc*w)/100;
   unfill = w - fill;
   snprintf(buffer, 255, "^fg(%s)^r(%dx%d)^fg(%s)^r(%dx%d)^fg()", fg, fill, h, bg, unfill, h);
}

static int printmpd() {
   if (mpd_init() && mpd_update_status() != -1) {
      mpd_now_playing();
      mpd_quit();
      return 1;
   }
   return 0;
}

static void printpacman() {
   static char buffer[5];
   static int times = 1;

   if (--times==0) {
      execget("pacup -u 2>/dev/null | wc -l", buffer, 5);
      times = 15;
   }

   printf("^bg(%s)^fg(%s)%s^fg()^bg()", UPDATE_BG, UPDATE_FG, buffer);
}

static void printcower() {
   static char buffer[5];
   static int times = 1;

   if (--times==0) {
      execget("cower -u 2>/dev/null | wc -l", buffer, 5);
      times = 15;
   }

   printf("^bg(%s)^fg(%s)%s^fg()^bg()", COWER_BG, COWER_FG, buffer);
}

static void printvolume(snd_mixer_elem_t *mixer) {
   char buffer[256]; int volume;
   volume = getvolume(mixer);
   gdbar(buffer, volume, VOL_FG, VOL_BG, 35, 9);
   printf("%s^bg(%s)^fg(%s)%3d%%^fg()^bg()",
         buffer, VOL_PERC_BG, VOL_PERC_FG, volume);
}

static void printbattery() {
   char state[13], buffer[256], *color; int charge;
   if (!batterystate(state) || !batterycharge(&charge)) {
      printf("FAIL");
      return;
   }
   if (!batcmp(state, "Charging"))  color = BAT_CHARGING;
   else if (charge > 65)            color = BAT_GOOD;
   else if (charge > 25)            color = BAT_AVEG;
   else                             color = BAT_BAD;
   gdbar(buffer, charge, color, BAT_BG, 35, 9);
   printf("%s %s %3d%%", !batcmp(state, "Charging")?"CHR":"BAT", buffer, charge);
}

static void printdate() {
   char date[12], clock[8];
   time_t rawtime; struct tm *timeinfo;
   time(&rawtime);
   timeinfo = localtime(&rawtime);
   strftime(date, sizeof(date)-1, "%a %d/%m", timeinfo);
   strftime(clock, sizeof(clock)-1, "%H:%M", timeinfo);
   printf("^bg(%s)^fg(%s)%s^bg(%s)^fg(%s)%s^fg()^bg()",
         DATE_BG, DATE_FG, date, TIME_BG, TIME_FG, clock);
}

int main(int argc, char **argv)
{
   snd_mixer_t *alsa; snd_mixer_elem_t *mixer;

   /* init alsa */
   if (!(alsa = alsainit("default")))
      die("Could not init ALSA (%s)\n", "default");
   /* get mixer */
   if (!(mixer = alsamixer(alsa, "Master")))
      die("Could not get mixer (%s)\n", "Master");

   /* statusbar itself (pipe to dzen) */
   while (1) {
      if (printmpd()) whitespace();
      printcower();
      whitespace();
      printpacman();
      whitespace();
      printvolume(mixer);
#if 0  /* PANDORA */
      whitespace();
      printbattery();
#endif
      whitespace();
      printdate();
      whitespace();
      eol();
      fflush(stdout);
      snd_mixer_wait(alsa, SLEEP_INTERVAL);
      snd_mixer_handle_events(alsa);
   }

   /* close alsa */
   alsaclose(alsa);
   return EXIT_SUCCESS;
}
