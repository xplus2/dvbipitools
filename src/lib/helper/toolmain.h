/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_TOOLMAIN_H
#define LIB_TOOLMAIN_H

/* startup banner: name, version, arch, build type, link mode */
void toolmain_print_banner(const char *tool_name, const char *tool_version, const char *build_arch, const char *build_type, const char *build_link);

/* daemon(1,1) if requested, tool_name tags failure log line. 0 ok, -1 failed (already logged) */
int toolmain_daemonize(int daemonize, const char *tool_name);

#define TOOLMAIN_STARTUP(argc, argv, cfg_ptr, parse_fn) \
  do { \
    args_status_t toolmain_st_; \
    log_set_color(log_color_prescan((argc), (argv))); \
    toolmain_print_banner(TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK); \
    toolmain_st_ = (parse_fn)((argc), (argv), (cfg_ptr)); \
    if (toolmain_st_ == ARGS_OK) log_set_color((log_color_t)(cfg_ptr)->color_mode); \
    if (toolmain_st_ == ARGS_HELP) return 0; \
    if (toolmain_st_ == ARGS_ERR) { \
      fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME); \
      return 2; \
    } \
  } while (0)

#endif
