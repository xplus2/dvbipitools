/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* build host tool, not shipped: embeds a text file as a C string literal.
   usage: gen_htdocs <input> <output.c> <symbol> */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  FILE *in, *out;
  const char *sym;
  int ch;
  size_t len = 0;
  int line_open = 0;

  if (argc != 4) {
    fprintf(stderr, "usage: %s <input> <output.c> <symbol>\n", argv[0]);
    return 1;
  }
  in = fopen(argv[1], "rb");
  if (!in) {
    perror(argv[1]);
    return 1;
  }
  out = fopen(argv[2], "w");
  if (!out) {
    perror(argv[2]);
    fclose(in);
    return 1;
  }
  sym = argv[3];

  fprintf(out, "/* generated from %s by gen_htdocs, do not edit */\n\n#include <stddef.h>\n\nconst char %s[] =\n", argv[1],
          sym);

  while ((ch = fgetc(in)) != EOF) {
    if (!line_open) {
      fputc('"', out);
      line_open = 1;
    }
    switch (ch) {
      case '"':
        fputs("\\\"", out);
        break;
      case '\\':
        fputs("\\\\", out);
        break;
      case '\n':
        fputs("\\n\"\n", out);
        line_open = 0;
        break;
      case '\r':
        fputs("\\r", out);
        break;
      case '\t':
        fputs("\\t", out);
        break;
      default:
        if (ch >= 0x20 && ch < 0x7f)
          fputc(ch, out);
        else
          fprintf(out, "\\x%02x\"\"", ch); /* close+reopen: hex escape must not absorb the next char */
        break;
    }
    len++;
  }
  if (line_open)
    fputc('"', out); /* close the line still open when the file ended */
  else if (len == 0)
    fputs("\"\"", out); /* empty file: declaration still needs a string literal */
  fprintf(out, ";\n\nconst size_t %s_len = %zu;\n", sym, len);

  fclose(in);
  return fclose(out) == 0 ? 0 : 1;
}
