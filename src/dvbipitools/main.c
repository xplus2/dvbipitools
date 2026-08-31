/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

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
  size_t i;
  for (i = 0; i < N_APPLETS; i++)
    if (strcmp(APPLETS[i].full_name, name) == 0)
      return &APPLETS[i];
  return NULL;
}

static const applet_t *find_by_full_or_short_name(const char *name) {
  size_t i;
  for (i = 0; i < N_APPLETS; i++)
    if (strcmp(APPLETS[i].full_name, name) == 0 || strcmp(APPLETS[i].short_name, name) == 0)
      return &APPLETS[i];
  return NULL;
}

static void print_deichkind(void) {
  static const unsigned char enc[] = {
      0x3e, 0x33, 0x2a, 0x33, 0x7a, 0x23, 0x3f, 0x3b, 0x32, 0x7b, 0x50, 0x31, 0x28, 0x3b, 0x2d, 0x3b,
      0x36, 0x36, 0x7a, 0x7c, 0x7a, 0x28, 0x3f, 0x37, 0x37, 0x33, 0x3e, 0x3f, 0x37, 0x37, 0x33, 0x7b,
      0x50};
  char buf[sizeof(enc) + 1];
  size_t i;
  for (i = 0; i < sizeof(enc); i++) buf[i] = (char)(enc[i] ^ 0x5a);
  buf[sizeof(enc)] = '\0';
  fputs(buf, stdout);
}

static void print_help(const char *invoked_as) {
  size_t i;
  fprintf(stderr, "%s - dvbipitools multicall binary (v%s)\n\n", TOOL_NAME, TOOL_VERSION);
  fprintf(stderr, "usage: %s <tool>   [args...]   full name\n", invoked_as);
  fprintf(stderr, "       %s <short>  [args...]   short form\n", invoked_as);
  fprintf(stderr, "       <toolname>  [args...]   via symlink or hardlink to this binary\n\n");
  fprintf(stderr, "applets:\n");
  for (i = 0; i < N_APPLETS; i++)
    fprintf(stderr, "  %-16s%s\n", APPLETS[i].full_name, APPLETS[i].short_name);
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
  if (applet)
    return applet->main_fn(argc, argv);

  if (strcmp(base, "dvbipitools") == 0 || strcmp(base, "dipi") == 0) {
    if (argc < 2) {
      print_help(base);
      return 0;
    }
    applet = find_by_full_or_short_name(argv[1]);
    if (applet)
      return applet->main_fn(argc - 1, argv + 1);
    fprintf(stderr, "%s: no applet named '%s'\n\n", base, argv[1]);
    print_help(base);
    return 2;
  }

  fprintf(stderr, "%s: not invoked under a known name\n\n", base);
  print_help(base);
  return 2;
}
