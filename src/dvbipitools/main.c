/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "version.h"

int dipibcg_main(int argc, char **argv);
int dipibim_main(int argc, char **argv);
#ifdef DVBIPITOOLS_HAVE_CAM378
int dipicam378_main(int argc, char **argv);
#endif
#ifdef DVBIPITOOLS_HAVE_DESCRAMBLE
int dipidescramble_main(int argc, char **argv);
#endif
int dipifccret_main(int argc, char **argv);
int dipimetrics_main(int argc, char **argv);
int dipiradiohead_main(int argc, char **argv);
int dipirec_main(int argc, char **argv);
#ifdef DVBIPITOOLS_HAVE_RIST
int dipirist_main(int argc, char **argv);
#endif
#ifdef DVBIPITOOLS_HAVE_SRT
int dipisrt_main(int argc, char **argv);
#endif
int dipiscan_main(int argc, char **argv);
int dipisds_main(int argc, char **argv);
int dipitvhead_main(int argc, char **argv);
int dipixmltv_main(int argc, char **argv);
int dipixy_main(int argc, char **argv);

typedef int (*applet_main_t)(int argc, char **argv);

typedef struct {
  const char *full_name;
  const char *short_name;
  applet_main_t main_fn;
} applet_t;

static const applet_t APPLETS[] = {
  {"dipibcg", "bcg", dipibcg_main},
  {"dipibim", "bim", dipibim_main},
#ifdef DVBIPITOOLS_HAVE_CAM378
  {"dipicam378", "cam378", dipicam378_main},
#endif
#ifdef DVBIPITOOLS_HAVE_DESCRAMBLE
  {"dipidescramble", "descramble", dipidescramble_main},
#endif
  {"dipifccret", "fccret", dipifccret_main},
  {"dipimetrics", "metrics", dipimetrics_main},
  {"dipiradiohead", "radiohead", dipiradiohead_main},
  {"dipirec", "rec", dipirec_main},
#ifdef DVBIPITOOLS_HAVE_RIST
  {"dipirist", "rist", dipirist_main},
#endif
#ifdef DVBIPITOOLS_HAVE_SRT
  {"dipisrt", "srt", dipisrt_main},
#endif
  {"dipiscan", "scan", dipiscan_main},
  {"dipisds", "sds", dipisds_main},
  {"dipitvhead", "tvhead", dipitvhead_main},
  {"dipixmltv", "xmltv", dipixmltv_main},
  {"dipixy", "xy", dipixy_main},
};

#define N_APPLETS (sizeof(APPLETS) / sizeof(APPLETS[0]))

static const char *basename_of(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static const applet_t *find_by_full_name(const char *name) {
  for (size_t i = 0; i < N_APPLETS; i++) if (strcmp(APPLETS[i].full_name, name) == 0) return &APPLETS[i];
  return NULL;
}

static const applet_t *find_by_full_or_short_name(const char *name) {
  for (size_t i = 0; i < N_APPLETS; i++)
    if (strcmp(APPLETS[i].full_name, name) == 0 || strcmp(APPLETS[i].short_name, name) == 0) return &APPLETS[i];
  return NULL;
}

static int resolve_self(const char *argv0, char *buf, size_t size) {
  ssize_t n = readlink("/proc/self/exe", buf, size - 1);
  if (n > 0 && (size_t)n < size) {
    buf[n] = '\0';
    return 0;
  }
  if (argv0[0] == '/' && strlen(argv0) < size) {
    strcpy(buf, argv0);
    return 0;
  }
  return -1;
}

static int install_applets(const char *argv0, const char *dir, int hard) {
  char self[PATH_MAX];
  char path[PATH_MAX];
  struct stat st;
  int failed = 0;

  if (stat(dir, &st) != 0) {
    fprintf(stderr, "dvbipitools: %s: %s\n", dir, strerror(errno));
    return 1;
  }
  if (!S_ISDIR(st.st_mode)) {
    fprintf(stderr, "dvbipitools: %s: not a directory\n", dir);
    return 1;
  }
  if (resolve_self(argv0, self, sizeof(self)) != 0) {
    fprintf(stderr, "dvbipitools: cannot resolve own path, '%s' is not absolute\n", argv0);
    return 1;
  }
  for (size_t i = 0; i < N_APPLETS; i++) {
    int n = snprintf(path, sizeof(path), "%s/%s", dir, APPLETS[i].full_name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
      fprintf(stderr, "dvbipitools: %s/%s: path too long\n", dir, APPLETS[i].full_name);
      failed = 1;
      continue;
    }
    if ((hard ? link(self, path) : symlink(self, path)) == 0)
      continue;
    if (errno == EEXIST) {
      fprintf(stderr, "dvbipitools: %s: exists, skipped\n", path);
      continue;
    }
    fprintf(stderr, "dvbipitools: %s: %s\n", path, strerror(errno));
    if (errno == EXDEV) return 1;
    failed = 1;
  }
  return failed;
}

static void list_applets(void) {
  for (size_t i = 0; i < N_APPLETS; i++) puts(APPLETS[i].full_name);
}

static int run_option(const char *base, int argc, char **argv) {
  int hard;
  if (strcmp(argv[1], "--list") == 0) {
    if (argc != 2) {
      fprintf(stderr, "usage: %s --list\n", base);
      return 2;
    }
    list_applets();
    return 0;
  }
  hard = argc == 4 && strcmp(argv[2], "-h") == 0;
  if (argc != 3 && !hard) {
    fprintf(stderr, "usage: %s --install [-h] DIR\n", base);
    return 2;
  }
  return install_applets(argv[0], argv[argc - 1], hard);
}

static void print_deichkind(void) {
  static const unsigned char enc[] = {
    0x3e, 0x33, 0x2a, 0x33, 0x7a, 0x23, 0x3f, 0x3b, 0x32, 0x7b, 0x50, 0x31, 0x28, 0x3b, 0x2d, 0x3b,
    0x36, 0x36, 0x7a, 0x7c, 0x7a, 0x28, 0x3f, 0x37, 0x37, 0x33, 0x3e, 0x3f, 0x37, 0x37, 0x33, 0x7b, 0x50
  };
  char buf[sizeof(enc) + 1];
  for (size_t i = 0; i < sizeof(enc); i++) buf[i] = (char)(enc[i] ^ 0x5a);
  buf[sizeof(enc)] = '\0';
  fputs(buf, stdout);
}

static void print_help(const char *invoked_as) {
  fprintf(stderr, "%s - dvbipitools multicall binary (v%s)\n\n", TOOL_NAME, TOOL_VERSION);
  fprintf(stderr, "usage: %s <tool>   [args...]   full name\n", invoked_as);
  fprintf(stderr, "       %s <short>  [args...]   short form\n", invoked_as);
  fprintf(stderr, "       <toolname>  [args...]   via symlink or hardlink to this binary\n");
  fprintf(stderr, "       %s --list               list applet names\n", invoked_as);
  fprintf(stderr, "       %s --install [-h] DIR   symlink (-h hardlink) all applets at DIR\n\n", invoked_as);
  fprintf(stderr, "applets:\n");
  for (size_t i = 0; i < N_APPLETS; i++) fprintf(stderr, "  %-16s%s\n", APPLETS[i].full_name, APPLETS[i].short_name);
}

int main(int argc, char **argv) {
  const char *base;
  const applet_t *applet;

  if (argc < 1 || !argv[0] || !*argv[0]) {
    print_help(TOOL_NAME);
    return 2;
  }
  base = basename_of(argv[0]);

  if (strcmp(base, "dipidipiyeah") == 0) {
    print_deichkind();
    return 0;
  }

  applet = find_by_full_name(base);
  if (applet) return applet->main_fn(argc, argv);

  if (strcmp(base, "dvbipitools") == 0 || strcmp(base, "dipi") == 0) {
    if (argc < 2) {
      print_help(base);
      return 0;
    }
    if (strcmp(argv[1], "--list") == 0 || strcmp(argv[1], "--install") == 0) return run_option(base, argc, argv);
    applet = find_by_full_or_short_name(argv[1]);
    if (applet) return applet->main_fn(argc - 1, argv + 1);
    fprintf(stderr, "%s: no applet named '%s'\n\n", base, argv[1]);
    print_help(base);
    return 2;
  }

  fprintf(stderr, "%s: not invoked under a known name\n\n", base);
  print_help(base);
  return 2;
}
