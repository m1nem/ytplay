/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║                        Y T P L A Y                          ║
 * ║          YouTube CLI Player — Stream or Download             ║
 * ║        Cross-platform: Linux · macOS · Windows              ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  ytplay.c  —  Search YouTube, stream or download videos.
 *
 *  Build:
 *    Linux/macOS:  gcc -O2 -o ytplay ytplay.c
 *    Windows:      gcc -O2 -o ytplay.exe ytplay.c
 *                  cl ytplay.c /Fe:ytplay.exe
 *
 *  Dependencies (must be in PATH):
 *    - yt-dlp   https://github.com/yt-dlp/yt-dlp
 *    - mpv / vlc / ffplay / iina
 */

/* popen/pclose on POSIX */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ─── Platform ───────────────────────────────────────────────── */
#ifdef _WIN32
#  define PLATFORM_WINDOWS
#  include <windows.h>
#  include <direct.h>
#  define PATH_SEP   "\\"
#  define DEVNULL    "NUL"
#  define POPEN(c,m) _popen(c,m)
#  define PCLOSE(f)  _pclose(f)
#  define MKDIR(p)   _mkdir(p)
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  define PATH_SEP   "/"
#  define DEVNULL    "/dev/null"
#  define POPEN(c,m) popen(c,m)
#  define PCLOSE(f)  pclose(f)
#  define MKDIR(p)   mkdir(p,0700)
#  ifdef __APPLE__
#    define PLATFORM_MACOS
#  else
#    define PLATFORM_LINUX
#  endif
#endif

/* ─── ANSI colours ───────────────────────────────────────────── */
static int g_color = 1;
#define C_RED (g_color?"\033[1;31m":"")
#define C_GRN (g_color?"\033[1;32m":"")
#define C_YLW (g_color?"\033[1;33m":"")
#define C_CYN (g_color?"\033[1;36m":"")
#define C_MAG (g_color?"\033[1;35m":"")
#define C_BLD (g_color?"\033[1m"   :"")
#define C_DIM (g_color?"\033[2m"   :"")
#define C_RST (g_color?"\033[0m"   :"")

/* ─── Constants ──────────────────────────────────────────────── */
#define YTPLAY_VERSION  "1.4.0"
#define MAX_RESULTS     25
#define MAX_CMD         8192
#define DEFAULT_RESULTS 8
#define DEFAULT_QUALITY "bestvideo[height<=1080]+bestaudio/best[height<=1080]"
#define JSON_LINE_MAX   (512*1024)   /* 512 KB — fits any yt-dlp JSON line */

/* ─── Structures ─────────────────────────────────────────────── */
typedef struct {
    char title[512];
    char id[32];
    char duration[24];
    char views[16];
    char channel[128];
} VideoResult;

typedef struct {
    char query[2048];
    int  num_results;

    char player[64];
    char quality[256];
    int  stream;
    int  audio_only;

    char output_dir[1024];
    int  keep;
    char subtitle_lang[8];

    char extra_player[512];
    char extra_ytdlp[512];

    int  no_banner;
    int  quiet;
    int  verbose;
    int  direct_play;
} Config;

/* ─── Globals ────────────────────────────────────────────────── */
static VideoResult g_results[MAX_RESULTS];
static int         g_nresults = 0;

/* ─── Logging helpers ────────────────────────────────────────── */
static void die(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s[ERROR]%s ", C_RED, C_RST);
    va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
    fputs("\n", stderr);
    exit(1);
}
static void info_msg(const char *fmt, ...) {
    va_list ap;
    printf("%s::%s ", C_CYN, C_RST);
    va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
    putchar('\n');
}
static void ok_msg(const char *fmt, ...) {
    va_list ap;
    printf("%s[+]%s ", C_GRN, C_RST);
    va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
    putchar('\n');
}
static void warn_msg(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s[!]%s ", C_YLW, C_RST);
    va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
    fputs("\n", stderr);
}

static void trim_nl(char *s) {
    size_t n = strlen(s);
    while (n>0 && (s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' ')) s[--n]='\0';
}

/* ─── Tiny JSON field extractor ──────────────────────────────── */
/*
 *  Finds the value of `key` in a flat JSON object (no nesting needed).
 *  Handles string values (quoted) and numeric values (unquoted).
 *  Returns 1 on success, 0 on missing/null.
 */
static int json_get(const char *json, const char *key, char *out, size_t outlen) {
    char pat[256];
    /* match  "key"  followed by optional whitespace + colon */
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        /* make sure it really is a key (preceded by '{' or ',') */
        const char *before = p - 1;
        while (before > json && (*before==' '||*before=='\t')) before--;
        if (before >= json && *before != '{' && *before != ',') { p++; continue; }

        p += strlen(pat);
        while (*p==' '||*p=='\t') p++;
        if (*p != ':') continue;
        p++;
        while (*p==' '||*p=='\t') p++;

        if (*p == '"') {
            /* string value */
            p++;
            size_t i = 0;
            while (*p && *p!='"' && i<outlen-1) {
                if (*p=='\\' && *(p+1)) {
                    p++;
                    switch (*p) {
                        case 'n':  out[i++]='\n'; break;
                        case 't':  out[i++]='\t'; break;
                        case '\\': out[i++]='\\'; break;
                        case '"':  out[i++]='"';  break;
                        case '/':  out[i++]='/';  break;
                        default:   out[i++]='?';  break;
                    }
                    p++;
                } else out[i++] = *p++;
            }
            out[i] = '\0';
            return 1;
        } else if (isdigit((unsigned char)*p) || *p=='-') {
            /* numeric value */
            size_t i = 0;
            while ((*p=='-'||isdigit((unsigned char)*p)) && i<outlen-1)
                out[i++] = *p++;
            out[i] = '\0';
            return 1;
        } else if (strncmp(p,"null",4)==0 || strncmp(p,"false",5)==0 ||
                   strncmp(p,"true",4)==0) {
            out[0] = '\0';
            return 0;
        }
        return 0;
    }
    return 0;
}

/* ─── Duration formatter ─────────────────────────────────────── */
static void fmt_duration(long secs, char *out, size_t n) {
    if (secs<=0) { snprintf(out,n,"?"); return; }
    long h=secs/3600, m=(secs%3600)/60, s=secs%60;
    if (h>0) snprintf(out,n,"%ld:%02ld:%02ld",h,m,s);
    else      snprintf(out,n,"%ld:%02ld",m,s);
}

/* ─── View count formatter ───────────────────────────────────── */
static void fmt_views(long v, char *out, size_t n) {
    if (v<=0)           snprintf(out,n,"?");
    else if (v>=1000000) snprintf(out,n,"%.1fM",v/1000000.0);
    else if (v>=1000)   snprintf(out,n,"%.1fK",v/1000.0);
    else                snprintf(out,n,"%ld",v);
}

/* ─── Misc utils ─────────────────────────────────────────────── */
static int cmd_exists(const char *c) {
#ifdef PLATFORM_WINDOWS
    char b[512]; snprintf(b,sizeof(b),"where \"%s\" >NUL 2>&1",c);
#else
    char b[512]; snprintf(b,sizeof(b),"command -v '%s' >/dev/null 2>&1",c);
#endif
    return system(b)==0;
}

static void get_tmpdir(char *out, size_t n) {
#ifdef PLATFORM_WINDOWS
    GetTempPathA((DWORD)n, out);
#else
    const char *t = getenv("TMPDIR");
    if (!t||!*t) t="/tmp";
    strncpy(out,t,n-1); out[n-1]='\0';
#endif
}

/* ─── Banner ─────────────────────────────────────────────────── */
static void print_banner(void) {
    printf("\n");
    printf("%s"
"  ██╗   ██╗████████╗██████╗ ██╗      █████╗ ██╗   ██╗\n"
"  ╚██╗ ██╔╝╚══██╔══╝██╔══██╗██║     ██╔══██╗╚██╗ ██╔╝\n"
"   ╚████╔╝    ██║   ██████╔╝██║     ███████║ ╚████╔╝ \n"
"    ╚██╔╝     ██║   ██╔═══╝ ██║     ██╔══██║  ╚██╔╝  \n"
"     ██║      ██║   ██║     ███████╗██║  ██║   ██║   \n"
"     ╚═╝      ╚═╝   ╚═╝     ╚══════╝╚═╝  ╚═╝   ╚═╝   \n"
    "%s", C_CYN, C_RST);
    printf("  %sStream · Search · Download  —  v%s%s\n\n",
           C_DIM, YTPLAY_VERSION, C_RST);

    /*
     * Box inner text (between │ and │), visible chars:
     * "  Linux · macOS · Windows  —  yt-dlp  " = 40 visible chars
     * Box border uses "────────────────────────────────────────" = 40 dashes
     * So │ + 40 chars + │ is perfectly flush.
     */
    printf("  %s┌────────────────────────────────────┐%s\n",C_MAG,C_RST);
    printf("  %s│%s  %sLinux%s - %smacOS%s - %sWindows%s - yt-dlp  %s│%s\n",
           C_MAG,C_RST, C_GRN,C_RST, C_YLW,C_RST, C_CYN,C_RST, C_MAG,C_RST);
    printf("  %s└────────────────────────────────────┘%s\n\n",C_MAG,C_RST);
}

/* ─── Help ───────────────────────────────────────────────────── */
static void print_help(const char *prog) {
    print_banner();
    printf("  %sUSAGE%s\n    %s [OPTIONS] <search query>\n\n", C_BLD, C_RST, prog);

    printf("  %sSEARCH%s\n", C_YLW, C_RST);
    printf("    %-28s  Results to show (default %d, max %d)\n","-n, --results <N>",DEFAULT_RESULTS,MAX_RESULTS);
    printf("    %-28s  Auto-play first result, skip menu\n",   "-1, --first");
    printf("    %-28s  Suppress ASCII banner\n\n",             "--no-banner");

    printf("  %sPLAYBACK%s\n", C_YLW, C_RST);
    printf("    %-28s  Stream — no file saved [default]\n",    "-s, --stream");
    printf("    %-28s  Download to temp dir then play\n",      "-d, --download");
    printf("    %-28s  Keep downloaded file\n",                 "-k, --keep");
    printf("    %-28s  Audio only\n",                           "-a, --audio-only");
    printf("    %-28s  yt-dlp format string\n",                 "-q, --quality <FMT>");
    printf("    %-28s  Preset: 4K\n",                           "    --4k");
    printf("    %-28s  Preset: 1080p [default]\n",              "    --1080");
    printf("    %-28s  Preset: 720p\n",                         "    --720");
    printf("    %-28s  Preset: 480p\n",                         "    --480");
    printf("    %-28s  Preset: 360p\n",                         "    --360");
    printf("    %-28s  Worst quality / fastest\n",              "    --worst");
    printf("    %-28s  Subtitles language  e.g. en, pl\n\n",   "    --subs <LANG>");

    printf("  %sPLAYER%s\n", C_YLW, C_RST);
    printf("    %-28s  mpv  vlc  ffplay  iina  mplayer\n", "-p, --player <NAME>");
    printf("    %-28s  Extra flags for player\n",          "    --player-args <ARGS>");
    printf("    %-28s  Extra flags for yt-dlp\n\n",        "    --ytdlp-args <ARGS>");

    printf("  %sOUTPUT%s\n", C_YLW, C_RST);
    printf("    %-28s  Download directory (default: /tmp/ytplay)\n","-o, --output <DIR>");
    printf("    %-28s  Disable colours\n",                          "    --no-color");
    printf("    %-28s  Minimal output\n",                           "    --quiet");
    printf("    %-28s  Show raw yt-dlp / player commands\n\n",      "-v, --verbose");

    printf("  %sEXAMPLES%s\n", C_GRN, C_RST);
    printf("    %s \"lofi hip hop\"\n",                prog);
    printf("    %s -1 \"rick astley\"\n",              prog);
    printf("    %s -d -k --1080 \"big buck bunny\"\n", prog);
    printf("    %s -a \"beethoven moonlight\"\n",      prog);
    printf("    %s -p vlc -n 15 \"documentaries\"\n",  prog);
    printf("    %s --subs pl \"ted talk\"\n\n",         prog);
}

/* ─── Player detection ───────────────────────────────────────── */
static void detect_player(char *out, size_t n) {
#ifdef PLATFORM_MACOS
    static const char *order[]={"iina","mpv","vlc","ffplay",NULL};
#elif defined(PLATFORM_WINDOWS)
    static const char *order[]={"mpv","vlc","ffplay",NULL};
#else
    static const char *order[]={"mpv","vlc","ffplay","mplayer",NULL};
#endif
    for (int i=0; order[i]; i++) {
        if (cmd_exists(order[i])) { strncpy(out,order[i],n-1); out[n-1]='\0'; return; }
    }
    strncpy(out,"mpv",n-1); out[n-1]='\0';
}

/* ─── Config defaults ────────────────────────────────────────── */
static void config_defaults(Config *c) {
    memset(c,0,sizeof(*c));
    c->num_results = DEFAULT_RESULTS;
    c->stream      = 1;
    strncpy(c->quality,DEFAULT_QUALITY,sizeof(c->quality)-1);
    char tmp[512];
    get_tmpdir(tmp,sizeof(tmp));
    snprintf(c->output_dir,sizeof(c->output_dir),"%s%sytplay",tmp,PATH_SEP);
}

/* ─── Argument parsing ───────────────────────────────────────── */
static void parse_args(int argc, char **argv, Config *c) {
    if (argc<2) { print_help(argv[0]); exit(0); }
#define NEED() do{ if(++i>=argc) die("'%s' needs an argument",a); }while(0)
    for (int i=1; i<argc; i++) {
        const char *a=argv[i];
        if (!strcmp(a,"--help")||!strcmp(a,"-h"))      { print_help(argv[0]); exit(0); }
        else if (!strcmp(a,"--version"))                { printf("ytplay %s\n",YTPLAY_VERSION); exit(0); }
        else if (!strcmp(a,"-n")||!strcmp(a,"--results")){ NEED(); c->num_results=atoi(argv[i]); if(c->num_results<1)c->num_results=1; if(c->num_results>MAX_RESULTS)c->num_results=MAX_RESULTS; }
        else if (!strcmp(a,"-1")||!strcmp(a,"--first")) { c->direct_play=1; c->num_results=1; }
        else if (!strcmp(a,"-s")||!strcmp(a,"--stream"))   c->stream=1;
        else if (!strcmp(a,"-d")||!strcmp(a,"--download")) c->stream=0;
        else if (!strcmp(a,"-k")||!strcmp(a,"--keep"))     c->keep=1;
        else if (!strcmp(a,"-a")||!strcmp(a,"--audio-only")){ c->audio_only=1; strncpy(c->quality,"bestaudio",sizeof(c->quality)-1); }
        else if (!strcmp(a,"-q")||!strcmp(a,"--quality"))  { NEED(); strncpy(c->quality,argv[i],sizeof(c->quality)-1); }
        else if (!strcmp(a,"--4k"))   strncpy(c->quality,"bestvideo[height<=2160]+bestaudio/best",sizeof(c->quality)-1);
        else if (!strcmp(a,"--1080")) strncpy(c->quality,"bestvideo[height<=1080]+bestaudio/best[height<=1080]",sizeof(c->quality)-1);
        else if (!strcmp(a,"--720"))  strncpy(c->quality,"bestvideo[height<=720]+bestaudio/best[height<=720]",sizeof(c->quality)-1);
        else if (!strcmp(a,"--480"))  strncpy(c->quality,"bestvideo[height<=480]+bestaudio/best[height<=480]",sizeof(c->quality)-1);
        else if (!strcmp(a,"--360"))  strncpy(c->quality,"bestvideo[height<=360]+bestaudio/best[height<=360]",sizeof(c->quality)-1);
        else if (!strcmp(a,"--worst"))strncpy(c->quality,"worst",sizeof(c->quality)-1);
        else if (!strcmp(a,"--subs")) { NEED(); strncpy(c->subtitle_lang,argv[i],sizeof(c->subtitle_lang)-1); }
        else if (!strcmp(a,"-p")||!strcmp(a,"--player"))     { NEED(); strncpy(c->player,argv[i],sizeof(c->player)-1); }
        else if (!strcmp(a,"--player-args"))                  { NEED(); strncpy(c->extra_player,argv[i],sizeof(c->extra_player)-1); }
        else if (!strcmp(a,"--ytdlp-args"))                   { NEED(); strncpy(c->extra_ytdlp,argv[i],sizeof(c->extra_ytdlp)-1); }
        else if (!strcmp(a,"-o")||!strcmp(a,"--output"))     { NEED(); strncpy(c->output_dir,argv[i],sizeof(c->output_dir)-1); }
        else if (!strcmp(a,"--no-color"))  g_color=0;
        else if (!strcmp(a,"--quiet"))     c->quiet=1;
        else if (!strcmp(a,"-v")||!strcmp(a,"--verbose")) c->verbose=1;
        else if (!strcmp(a,"--no-banner")) c->no_banner=1;
        else if (a[0]=='-')                die("Unknown option: %s  (use --help)",a);
        else {
            size_t qlen=strlen(c->query);
            if (qlen>0 && qlen<sizeof(c->query)-2) { c->query[qlen]=' '; c->query[qlen+1]='\0'; }
            strncat(c->query,a,sizeof(c->query)-strlen(c->query)-1);
        }
    }
#undef NEED
    if (!strlen(c->query)) die("No search query provided. Use --help.");
}

/* ─── YouTube search ─────────────────────────────────────────── */
/*
 *  We use:
 *    yt-dlp --flat-playlist --dump-json "ytsearch<N>:<query>"
 *
 *  --flat-playlist  →  one compact JSON object per line, very fast
 *                       (does NOT hit each video page)
 *  --dump-json      →  structured JSON we can parse reliably
 *
 *  Fields we extract from the flat JSON:
 *    id, title, duration (seconds), view_count, channel / uploader
 *
 *  The full watch URL is reconstructed as:
 *    https://www.youtube.com/watch?v=<id>
 */
static int search_youtube(Config *c) {
    /* shell-safe query: wrap in single quotes, escape ' inside */
    char safe_q[4096]; size_t j=0;
    for (const char *s=c->query; *s && j<sizeof(safe_q)-5; s++) {
        if (*s=='\'') { safe_q[j++]='\''; safe_q[j++]='\\'; safe_q[j++]='\''; safe_q[j++]='\''; }
        else safe_q[j++]=*s;
    }
    safe_q[j]='\0';

    char cmd[MAX_CMD];
    snprintf(cmd,sizeof(cmd),
             "yt-dlp --no-warnings --flat-playlist --dump-json"
             " 'ytsearch%d:%s'"
             " 2>" DEVNULL,
             c->num_results, safe_q);

    if (c->verbose)
        printf("%s[yt-dlp]%s %s\n\n",C_DIM,C_RST,cmd);

    FILE *fp = POPEN(cmd,"r");
    if (!fp) die("Failed to launch yt-dlp. Is it installed and in PATH?");

    char *line = (char*)malloc(JSON_LINE_MAX);
    if (!line) die("Out of memory");

    g_nresults=0;
    while (fgets(line, JSON_LINE_MAX, fp) && g_nresults < MAX_RESULTS) {
        if (line[0]!='{') continue;

        VideoResult *r = &g_results[g_nresults];
        char tmp[64];

        if (!json_get(line,"title",  r->title,   sizeof(r->title)))   continue;
        if (!json_get(line,"id",     r->id,       sizeof(r->id)))      continue;

        if (json_get(line,"duration",tmp,sizeof(tmp)) && tmp[0])
            fmt_duration(atol(tmp), r->duration, sizeof(r->duration));
        else
            strncpy(r->duration,"?",sizeof(r->duration));

        if (json_get(line,"view_count",tmp,sizeof(tmp)) && tmp[0])
            fmt_views(atol(tmp), r->views, sizeof(r->views));
        else
            strncpy(r->views,"?",sizeof(r->views));

        if (!json_get(line,"channel",  r->channel,sizeof(r->channel)))
            if (!json_get(line,"uploader",r->channel,sizeof(r->channel)))
                strncpy(r->channel,"Unknown",sizeof(r->channel));

        g_nresults++;
    }

    free(line);
    PCLOSE(fp);
    return g_nresults;
}

/* ─── Print results ──────────────────────────────────────────── */
static void print_results(Config *c) {
    printf("\n");
    char qs[48]; strncpy(qs,c->query,46); qs[46]='\0';
    if (strlen(c->query)>46) strcat(qs,"…");

    printf("  %s┌────────────────────────────────────────────────────────────────┐%s\n",C_CYN,C_RST);
    printf("  %s│%s  Search : %s%-53s%s %s│%s\n",C_CYN,C_RST,C_BLD,qs,C_RST,C_CYN,C_RST);
    printf("  %s│%s  Results: %s%d%s%-54s%s│%s\n",C_CYN,C_RST,C_GRN,g_nresults,C_RST,"",C_CYN,C_RST);
    printf("  %s└────────────────────────────────────────────────────────────────┘%s\n\n",C_CYN,C_RST);

    for (int i=0; i<g_nresults; i++) {
        VideoResult *r=&g_results[i];
        char t[62]; strncpy(t,r->title,60); t[60]='\0';
        if (strlen(r->title)>60) strcat(t,"…");
        char ch[24]; strncpy(ch,r->channel,22); ch[22]='\0';
        if (strlen(r->channel)>22) strcat(ch,"…");

        printf("  %s[%2d]%s %s%s%s\n",      C_YLW,i+1,C_RST, C_BLD,t,C_RST);
        printf("       %s%-24s%s ⏱ %s%-9s%s 👁 %s%s%s\n",
               C_DIM,ch,C_RST, C_GRN,r->duration,C_RST, C_MAG,r->views,C_RST);
        printf("\n");
    }
}

/* ─── Interactive prompt ─────────────────────────────────────── */
static int prompt_choice(void) {
    char buf[32];
    printf("  %s╔══════════════════════════════════════╗%s\n",C_CYN,C_RST);
    printf("  %s║%s  Enter number to play  [0 = quit]    %s║%s\n",C_CYN,C_RST,C_CYN,C_RST);
    printf("  %s╚══════════════════════════════════════╝%s\n",C_CYN,C_RST);
    printf("  %s▶%s  ",C_GRN,C_RST); fflush(stdout);
    if (!fgets(buf,sizeof(buf),stdin)) return 0;
    return atoi(buf);
}

/* ─── Play a video ───────────────────────────────────────────── */
static int play_video(Config *c, VideoResult *r) {
    char url[128];
    snprintf(url,sizeof(url),"https://www.youtube.com/watch?v=%s",r->id);

    char cmd[MAX_CMD];

    if (c->stream) {
        /* Stream mode */
        if (!c->quiet)
            info_msg("Streaming: %s%s%s",C_BLD,r->title,C_RST);

        int native = (!strcmp(c->player,"mpv")||!strcmp(c->player,"iina"));
        if (native) {
            snprintf(cmd,sizeof(cmd),
                     "%s --ytdl-format='%s'%s %s '%s'",
                     c->player,
                     c->quality,
                     strlen(c->subtitle_lang)?" --sub-auto=all":"",
                     c->extra_player,
                     url);
        } else {
            /* get raw URL via yt-dlp -g */
            char gcmd[MAX_CMD];
            snprintf(gcmd,sizeof(gcmd),
                     "yt-dlp --no-warnings -g -f '%s' '%s' 2>" DEVNULL,
                     c->quality, url);
            if (c->verbose)
                printf("%s[yt-dlp -g]%s %s\n",C_DIM,C_RST,gcmd);
            FILE *fp = POPEN(gcmd,"r");
            if (!fp) die("Failed to get stream URL.");
            char surl[4096]="";
            if (!fgets(surl,sizeof(surl),fp)) die("yt-dlp returned no stream URL.");
            trim_nl(surl);
            PCLOSE(fp);
            snprintf(cmd,sizeof(cmd),"%s %s '%s'",c->player,c->extra_player,surl);
        }

        if (c->verbose)
            printf("%s[player]%s %s\n\n",C_DIM,C_RST,cmd);

        return system(cmd);

    } else {
        /* Download mode */
        if (!c->quiet)
            info_msg("Downloading: %s%s%s  →  %s",C_BLD,r->title,C_RST,c->output_dir);

        MKDIR(c->output_dir);

        char dl_cmd[MAX_CMD];
        snprintf(dl_cmd,sizeof(dl_cmd),
                 "yt-dlp --no-warnings -f '%s' -o '%s" PATH_SEP "%%(title)s.%%(ext)s' %s '%s'",
                 c->quality, c->output_dir,
                 strlen(c->extra_ytdlp)?c->extra_ytdlp:"",
                 url);

        if (c->verbose)
            printf("%s[yt-dlp]%s %s\n\n",C_DIM,C_RST,dl_cmd);

        if (system(dl_cmd)!=0) die("yt-dlp download failed.");

        /* find newest file */
        char find_cmd[MAX_CMD], dl_path[2048]="";
#ifdef PLATFORM_WINDOWS
        snprintf(find_cmd,sizeof(find_cmd),"dir /b /o-d \"%s\" 2>NUL",c->output_dir);
        FILE *dfp=POPEN(find_cmd,"r");
        if (dfp) { char fn[1024]; if(fgets(fn,sizeof(fn),dfp)){trim_nl(fn);snprintf(dl_path,sizeof(dl_path),"%s\\%s",c->output_dir,fn);} PCLOSE(dfp); }
#else
        snprintf(find_cmd,sizeof(find_cmd),"ls -t '%s' 2>/dev/null | head -1",c->output_dir);
        FILE *dfp=POPEN(find_cmd,"r");
        if (dfp) { char fn[1024]; if(fgets(fn,sizeof(fn),dfp)){trim_nl(fn);snprintf(dl_path,sizeof(dl_path),"%s/%s",c->output_dir,fn);} PCLOSE(dfp); }
#endif
        if (!dl_path[0]) die("Could not find downloaded file in %s",c->output_dir);

        ok_msg("Saved: %s%s%s",C_GRN,dl_path,C_RST);

        snprintf(cmd,sizeof(cmd),"%s %s '%s'",c->player,c->extra_player,dl_path);
        if (c->verbose) printf("%s[player]%s %s\n\n",C_DIM,C_RST,cmd);

        info_msg("Opening with %s%s%s ...",C_BLD,c->player,C_RST);
        int ret=system(cmd);

        if (!c->keep) { if(!c->quiet) info_msg("Removing temp file..."); remove(dl_path); }
        return ret;
    }
}

/* ─── Dependency check ───────────────────────────────────────── */
static void check_deps(Config *c) {
    if (!cmd_exists("yt-dlp"))
        die("yt-dlp not found.\n"
            "  Linux/macOS : pip install yt-dlp   or   brew install yt-dlp\n"
            "  Windows     : winget install yt-dlp\n"
            "  Docs        : https://github.com/yt-dlp/yt-dlp");
    if (!cmd_exists(c->player)) {
        warn_msg("Player '%s' not found, auto-detecting...",c->player);
        detect_player(c->player,sizeof(c->player));
        if (!cmd_exists(c->player))
            die("No supported player found. Install mpv, vlc, or ffplay.");
        warn_msg("Using '%s'.",c->player);
    }
}

/* ─── main ───────────────────────────────────────────────────── */
int main(int argc, char **argv) {
#ifdef PLATFORM_WINDOWS
    HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE); DWORD m=0;
    if (GetConsoleMode(h,&m)) SetConsoleMode(h,m|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    if (!isatty(STDOUT_FILENO)) g_color=0;
#endif

    Config c;
    config_defaults(&c);
    parse_args(argc, argv, &c);

    if (!c.no_banner && !c.quiet) print_banner();

    if (!strlen(c.player)) detect_player(c.player,sizeof(c.player));
    check_deps(&c);

    if (!c.quiet)
        info_msg("Searching YouTube for: %s%s%s ...",C_BLD,c.query,C_RST);

    if (search_youtube(&c)==0)
        die("No results found for '%s'\n"
            "  Tip: run with -v to print the exact yt-dlp command.",c.query);

    if (c.direct_play) {
        ok_msg("Playing: %s%s%s",C_BLD,g_results[0].title,C_RST);
        return play_video(&c,&g_results[0]);
    }

    print_results(&c);
    int choice=prompt_choice();
    if (choice==0) { printf("\n  %sGoodbye!%s\n\n",C_CYN,C_RST); return 0; }
    if (choice<1||choice>g_nresults) die("Invalid choice: %d",choice);

    VideoResult *sel=&g_results[choice-1];
    printf("\n");
    ok_msg("Selected : %s%s%s", C_BLD, sel->title, C_RST);
    ok_msg("Mode     : %s%s%s", C_GRN, c.stream?"Stream":"Download", C_RST);
    ok_msg("Quality  : %s%s%s", C_YLW, c.quality, C_RST);
    ok_msg("Player   : %s%s%s\n", C_MAG, c.player, C_RST);

    int ret=play_video(&c,sel);
    if (ret!=0) warn_msg("Player exited with code %d",ret);
    printf("\n  %s[ytplay]%s Done. 🎬\n\n",C_CYN,C_RST);
    return ret;
}
