/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/org/documents/epl-2.0/EPL-2.0.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#include "config.h"
#include <assert.h>
#include <common/globals.h>
#include <common/render.h>
#include <common/types.h>
#include <common/usershape.h>
#include <common/utils.h>
#include <errno.h>
#include <gvc/gvcint.h>
#include <gvc/gvcproc.h>
#include <gvc/gvplugin.h>
#include <gvc/gvplugin_loadimage.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/agxbuf.h>
#include <util/alloc.h>
#include <util/gv_ctype.h>
#include <util/gv_fopen.h>
#include <util/optional.h>
#include <util/streq.h>
#include <util/strview.h>

static Dict_t *ImageDict;

typedef struct {
  char *template;
  size_t size;
  imagetype_t type;
  char *stringtype;
} knowntype_t;

#define HDRLEN 20

#define PNG_MAGIC "\x89PNG\x0D\x0A\x1A\x0A"
#define PS_MAGIC "%!PS-Adobe-"
#define BMP_MAGIC "BM"
#define GIF_MAGIC "GIF8"
#define JPEG_MAGIC "\xFF\xD8\xFF"
#define PDF_MAGIC "%PDF-"
#define EPS_MAGIC "\xC5\xD0\xD3\xC6"
#define XML_MAGIC "<?xml"
#define SVG_MAGIC "<svg"
#define RIFF_MAGIC "RIFF"
#define WEBP_MAGIC "WEBP"
#define ICO_MAGIC "\x00\x00\x01\x00"

static knowntype_t knowntypes[] = {
    {
        PNG_MAGIC,
        sizeof(PNG_MAGIC) - 1,
        FT_PNG,
        "png",
    },
    {
        PS_MAGIC,
        sizeof(PS_MAGIC) - 1,
        FT_PS,
        "ps",
    },
    {
        BMP_MAGIC,
        sizeof(BMP_MAGIC) - 1,
        FT_BMP,
        "bmp",
    },
    {
        GIF_MAGIC,
        sizeof(GIF_MAGIC) - 1,
        FT_GIF,
        "gif",
    },
    {
        JPEG_MAGIC,
        sizeof(JPEG_MAGIC) - 1,
        FT_JPEG,
        "jpeg",
    },
    {
        PDF_MAGIC,
        sizeof(PDF_MAGIC) - 1,
        FT_PDF,
        "pdf",
    },
    {
        EPS_MAGIC,
        sizeof(EPS_MAGIC) - 1,
        FT_EPS,
        "eps",
    },
    {
        XML_MAGIC,
        sizeof(XML_MAGIC) - 1,
        FT_XML,
        "xml",
    },
    {
        RIFF_MAGIC,
        sizeof(RIFF_MAGIC) - 1,
        FT_RIFF,
        "riff",
    },
    {
        ICO_MAGIC,
        sizeof(ICO_MAGIC) - 1,
        FT_ICO,
        "ico",
    },
};

static imagetype_t imagetype(usershape_t *us) {
  char header[HDRLEN] = {0};

  if (us->f && fread(header, 1, HDRLEN, us->f) == HDRLEN) {
    for (size_t i = 0; i < sizeof(knowntypes) / sizeof(knowntype_t); i++) {
      if (!memcmp(header, knowntypes[i].template, knowntypes[i].size)) {
        us->stringtype = knowntypes[i].stringtype;
        us->type = knowntypes[i].type;
        if (us->type == FT_XML) {
          // if we did not see the closing of the XML declaration, scan for it
          if (memchr(header, '>', HDRLEN) == NULL) {
            while (true) {
              int c = fgetc(us->f);
              if (c == EOF) {
                return us->type;
              } else if (c == '>') {
                break;
              }
            }
          }
          /* check for SVG in case of XML */
          char tag[sizeof(SVG_MAGIC) - 1] = {0};
          if (fread(tag, 1, sizeof(tag), us->f) != sizeof(tag)) {
            return us->type;
          }
          while (true) {
            if (memcmp(tag, SVG_MAGIC, sizeof(SVG_MAGIC) - 1) == 0) {
              us->stringtype = "svg";
              return (us->type = FT_SVG);
            }
            int c = fgetc(us->f);
            if (c == EOF) {
              return us->type;
            }
            memmove(&tag[0], &tag[1], sizeof(tag) - 1);
            tag[sizeof(tag) - 1] = (char)c;
          }
        } else if (us->type == FT_RIFF) {
          /* check for WEBP in case of RIFF */
          if (!memcmp(header + 8, WEBP_MAGIC, sizeof(WEBP_MAGIC) - 1)) {
            us->stringtype = "webp";
            return (us->type = FT_WEBP);
          }
        }
        return us->type;
      }
    }
  }

  us->stringtype = "(lib)";
  us->type = FT_NULL;

  return FT_NULL;
}

static bool get_int_lsb_first(FILE *f, size_t sz, int *val) {
  unsigned value = 0;
  for (size_t i = 0; i < sz; i++) {
    const int ch = fgetc(f);
    if (feof(f))
      return false;
    value |= (unsigned)ch << 8 * i;
  }
  if (value > INT_MAX) {
    return false;
  }
  *val = (int)value;
  return true;
}

static bool get_int_msb_first(FILE *f, size_t sz, int *val) {
  unsigned value = 0;
  for (size_t i = 0; i < sz; i++) {
    const int ch = fgetc(f);
    if (feof(f))
      return false;
    value <<= 8;
    value |= (unsigned)ch;
  }
  if (value > INT_MAX) {
    return false;
  }
  *val = (int)value;
  return true;
}

static double svg_units_convert(double n, char *u) {
  if (streq(u, "in"))
    return round(n * POINTS_PER_INCH);
  if (streq(u, "px"))
    return round(n * POINTS_PER_INCH / 96);
  if (streq(u, "pc"))
    return round(n * POINTS_PER_INCH / 6);
  if (streq(u, "pt") || streq(u, "\"")) /* ugly!!  - if there are no inits then
                                           the %2s get the trailing '"' */
    return round(n);
  if (streq(u, "cm"))
    return round(n * POINTS_PER_CM);
  if (streq(u, "mm"))
    return round(n * POINTS_PER_MM);
  return 0;
}

typedef struct {
  strview_t key;
  strview_t value;
} match_t;

static int find_attribute(const char *s, match_t *result) {

  // look for an attribute string matching ([a-z][a-zA-Z]*)="([^"]*)"
  for (size_t i = 0; s[i] != '\0';) {
    if (gv_islower(s[i])) {
      result->key.data = &s[i];
      result->key.size = 1;
      ++i;
      while (gv_isalpha(s[i])) {
        ++i;
        ++result->key.size;
      }
      if (s[i] == '=' && s[i + 1] == '"') {
        i += 2;
        result->value.data = &s[i];
        result->value.size = 0;
        while (s[i] != '"' && s[i] != '\0') {
          ++i;
          ++result->value.size;
        }
        if (s[i] == '"') {
          // found a valid attribute
          return 0;
        }
      }
    } else {
      ++i;
    }
  }

  // no attribute found
  return -1;
}

static void svg_size(usershape_t *us) {
  double n;
  char u[3];
  agxbuf line = {0};
  bool eof = false;

  // authoritative constraints we learned from `height` and `width`
  OPTIONAL(double) hard_height = {0};
  OPTIONAL(double) hard_width = {0};

  // fallback constraints we learned from `viewBox`
  OPTIONAL(double) soft_height = {0};
  OPTIONAL(double) soft_width = {0};

  rewind(us->f);
  while (!eof && (!hard_width.has_value || !hard_height.has_value)) {
    // read next line
    while (true) {
      int c = fgetc(us->f);
      if (c == EOF) {
        eof = true;
        break;
      } else if (c == '\n') {
        break;
      }
      agxbputc(&line, (char)c);
    }

    const char *re_string = agxbuse(&line);
    match_t match;
    while (find_attribute(re_string, &match) == 0) {
      re_string = match.value.data + match.value.size + 1;

      if (strview_str_eq(match.key, "width")) {
        char *value = strview_str(match.value);
        if (sscanf(value, "%lf%2s", &n, u) == 2) {
          OPTIONAL_SET(&hard_width, svg_units_convert(n, u));
        } else if (sscanf(value, "%lf", &n) == 1) {
          OPTIONAL_SET(&hard_width, svg_units_convert(n, "pt"));
        }
        free(value);
        if (hard_height.has_value)
          break;
      } else if (strview_str_eq(match.key, "height")) {
        char *value = strview_str(match.value);
        if (sscanf(value, "%lf%2s", &n, u) == 2) {
          OPTIONAL_SET(&hard_height, svg_units_convert(n, u));
        } else if (sscanf(value, "%lf", &n) == 1) {
          OPTIONAL_SET(&hard_height, svg_units_convert(n, "pt"));
        }
        free(value);
        if (hard_width.has_value)
          break;
      } else if (strview_str_eq(match.key, "viewBox")) {
        char *value = strview_str(match.value);
        double w, h;
        if (sscanf(value, "%*f %*f %lf %lf", &w, &h) == 2) {
          OPTIONAL_SET(&soft_width, w);
          OPTIONAL_SET(&soft_height, h);
        }
        free(value);
      }
    }

    // if we have reached the end of a line and have seen `viewBox` but not
    // `height` and/or `width`, let `viewBox` determine the dimensions
    if (soft_height.has_value && soft_width.has_value) {
      if (!hard_height.has_value) {
        OPTIONAL_SET(&hard_height, OPTIONAL_VALUE(soft_height));
      }
      if (!hard_width.has_value) {
        OPTIONAL_SET(&hard_width, OPTIONAL_VALUE(soft_width));
      }
      break;
    }
  }
  us->dpi = 0;
  const double h = OPTIONAL_VALUE_OR(hard_height, 0);
  const double w = OPTIONAL_VALUE_OR(hard_width, 0);
  assert(w >= 0 && w <= INT_MAX);
  us->w = (int)w;
  assert(h >= 0 && h <= INT_MAX);
  us->h = (int)h;
  agxbfree(&line);
}

static void png_size(usershape_t *us) {
  int w, h;

  us->dpi = 0;
  fseek(us->f, 16, SEEK_SET);
  if (get_int_msb_first(us->f, 4, &w) && get_int_msb_first(us->f, 4, &h)) {
    us->w = w;
    us->h = h;
  }
}

static void ico_size(usershape_t *us) {
  int w, h;

  us->dpi = 0;
  fseek(us->f, 6, SEEK_SET);
  if (get_int_msb_first(us->f, 1, &w) && get_int_msb_first(us->f, 1, &h)) {
    us->w = w;
    us->h = h;
  }
}

static void webp_size(usershape_t *us) {
  int w, h;

  us->dpi = 0;
  fseek(us->f, 15, SEEK_SET);
  if (fgetc(us->f) == 'X') { // VP8X
    fseek(us->f, 24, SEEK_SET);
    if (get_int_lsb_first(us->f, 4, &w) && get_int_lsb_first(us->f, 4, &h)) {
      us->w = w;
      us->h = h;
    }
  } else { // VP8
    fseek(us->f, 26, SEEK_SET);
    if (get_int_lsb_first(us->f, 2, &w) && get_int_lsb_first(us->f, 2, &h)) {
      us->w = w;
      us->h = h;
    }
  }
}

static void gif_size(usershape_t *us) {
  int w, h;

  us->dpi = 0;
  fseek(us->f, 6, SEEK_SET);
  if (get_int_lsb_first(us->f, 2, &w) && get_int_lsb_first(us->f, 2, &h)) {
    us->w = w;
    us->h = h;
  }
}

static void bmp_size(usershape_t *us) {
  int size_x_msw, size_x_lsw, size_y_msw, size_y_lsw;

  us->dpi = 0;
  fseek(us->f, 16, SEEK_SET);
  if (get_int_lsb_first(us->f, 2, &size_x_msw) &&
      get_int_lsb_first(us->f, 2, &size_x_lsw) &&
      get_int_lsb_first(us->f, 2, &size_y_msw) &&
      get_int_lsb_first(us->f, 2, &size_y_lsw)) {
    us->w = size_x_msw << 16 | size_x_lsw;
    us->h = size_y_msw << 16 | size_y_lsw;
  }
}

static void jpeg_size(usershape_t *us) {
  int marker, length, size_x, size_y;

  /* These are the markers that follow 0xff in the file.
   * Other markers implicitly have a 2-byte length field that follows.
   */
  static const unsigned char standalone_markers[] = {
      0x01,                         /* Temporary */
      0xd0, 0xd1, 0xd2, 0xd3,       /* Reset */
      0xd4, 0xd5, 0xd6, 0xd7, 0xd8, /* Start of image */
      0xd9,                         /* End of image */
  };

  us->dpi = 0;
  rewind(us->f);
  while (true) {
    /* Now we must be at a 0xff or at a series of 0xff's.
     * If that is not the case, or if we're at EOF, then there's
     * a parsing error.
     */
    if (!get_int_msb_first(us->f, 1, &marker)) {
      agwarningf("Parsing of \"%s\" failed\n", us->name);
      return;
    }

    if (marker == 0xff)
      continue;

    /* Ok.. marker now read. If it is not a stand-alone marker,
     * then continue. If it's a Start Of Frame (0xc?), then we're there.
     * If it's another marker with a length field, then skip ahead
     * over that length field.
     */

    /* A stand-alone... */
    if (memchr(standalone_markers, marker, sizeof(standalone_markers)))
      continue;

    /* Incase of a 0xc0 marker: */
    if (marker == 0xc0) {
      /* Skip length and 2 lengths. */
      if (fseek(us->f, 3, SEEK_CUR) == 0 &&
          get_int_msb_first(us->f, 2, &size_y) &&
          get_int_msb_first(us->f, 2, &size_x)) {

        /* Store length. */
        us->h = size_y;
        us->w = size_x;
      }
      return;
    }

    /* Incase of a 0xc2 marker: */
    if (marker == 0xc2) {
      /* Skip length and one more byte */
      if (fseek(us->f, 3, SEEK_CUR) != 0)
        return;

      /* Get length and store. */
      if (get_int_msb_first(us->f, 2, &size_y) &&
          get_int_msb_first(us->f, 2, &size_x)) {
        us->h = size_y;
        us->w = size_x;
      }
      return;
    }

    /* Any other marker is assumed to be followed by 2 bytes length. */
    if (!get_int_msb_first(us->f, 2, &length))
      return;

    fseek(us->f, length - 2, SEEK_CUR);
  }
}

static void ps_size(usershape_t *us) {
  char line[BUFSIZ];
  int lx, ly, ux, uy;
  char *linep;

  us->dpi = 72;
  rewind(us->f);
  bool saw_bb = false;
  while (fgets(line, sizeof(line), us->f)) {
    /* PostScript accepts \r as EOL, so using fgets () and looking for a
     * bounding box comment at the beginning doesn't work in this case.
     * As a heuristic, we first search for a bounding box comment in line.
     * This obviously fails if not all of the numbers make it into the
     * current buffer. This shouldn't be a problem, as the comment is
     * typically near the beginning, and so should be read within the first
     * BUFSIZ bytes (even on Windows where this is 512).
     */
    if (!(linep = strstr(line, "%%BoundingBox:")))
      continue;
    if (sscanf(linep, "%%%%BoundingBox: %d %d %d %d", &lx, &ly, &ux, &uy) ==
        4) {
      saw_bb = true;
      break;
    }
  }
  if (saw_bb) {
    us->x = lx;
    us->y = ly;
    us->w = ux - lx;
    us->h = uy - ly;
  }
}

typedef struct {
  FILE *fp;
  agxbuf scratch;
} stream_t;

static void skipWS(stream_t *str) {
  while (true) {
    const int c = getc(str->fp);
    if (gv_isspace(c)) {
      continue;
    }
    if (c != EOF) {
      (void)ungetc(c, str->fp);
    }
    break;
  }
}

static int scanNum(char *tok, double *dp) {
  char *endp;
  double d = strtod(tok, &endp);

  if (tok == endp)
    return 1;
  *dp = d;
  return 0;
}

static char *getNum(stream_t *str) {
  skipWS(str);
  while (true) {
    const int c = getc(str->fp);
    if (gv_isdigit(c) || c == '.') {
      agxbputc(&str->scratch, (char)c);
      continue;
    }
    if (c != EOF) {
      (void)ungetc(c, str->fp);
    }
    break;
  }
  return agxbuse(&str->scratch);
}

static int boxof(stream_t *str, boxf *bp) {
  skipWS(str);
  if (getc(str->fp) != '[')
    return 1;
  char *tok = getNum(str);
  if (scanNum(tok, &bp->LL.x))
    return 1;
  tok = getNum(str);
  if (scanNum(tok, &bp->LL.y))
    return 1;
  tok = getNum(str);
  if (scanNum(tok, &bp->UR.x))
    return 1;
  tok = getNum(str);
  if (scanNum(tok, &bp->UR.y))
    return 1;
  return 0;
}

/// scan a file until a string is found
///
/// This is essentially `strstr`, but taking a `FILE *` as the haystack instead
/// of a `char *`. Note that the position of `f` will be immediately after the
/// given string if this function returns `true`.
///
/// @param f File to seek
/// @param needle Substring to look for
/// @return True if the substring was found
static bool fstr(FILE *f, const char *needle) {
  assert(f != NULL);
  assert(needle != NULL);

  // the algorithm in this function only works if the needle’s characters are
  // distinct
  for (size_t i = 0; needle[i] != '\0'; ++i) {
    for (size_t j = i + 1; needle[j] != '\0'; ++j) {
      assert(needle[i] != needle[j]);
    }
  }

  for (size_t offset = 0;;) {
    if (needle[offset] == '\0') {
      return true;
    }
    const int c = getc(f);
    if (c == EOF) {
      break;
    }
    if (needle[offset] == c) {
      ++offset;
    } else if (needle[0] == c) {
      offset = 1;
    } else {
      offset = 0;
    }
  }

  return false;
}

static int bboxPDF(FILE *fp, boxf *bp) {
  static const char KEY[] = "/MediaBox";
  if (fstr(fp, KEY)) {
    stream_t str = {.fp = fp};
    const int rc = boxof(&str, bp);
    agxbfree(&str.scratch);
    return rc;
  }

  return 1;
}

static void pdf_size(usershape_t *us) {
  boxf bb;

  us->dpi = 0;
  rewind(us->f);
  if (!bboxPDF(us->f, &bb)) {
    us->x = bb.LL.x;
    us->y = bb.LL.y;
    us->w = bb.UR.x - bb.LL.x;
    us->h = bb.UR.y - bb.LL.y;
  }
}

static void usershape_close(void *p) {
  usershape_t *us = p;

  if (us->f)
    fclose(us->f);
  if (us->data && us->datafree)
    us->datafree(us);
  free(us);
}

static Dtdisc_t ImageDictDisc = {
    .key = offsetof(usershape_t, name),
    .size = -1,
    .freef = usershape_close,
};

usershape_t *gvusershape_find(const char *name) {
  assert(name);
  assert(name[0]);

  if (!ImageDict)
    return NULL;

  return dtmatch(ImageDict, name);
}

#define MAX_USERSHAPE_FILES_OPEN 50
bool gvusershape_file_access(usershape_t *us) {
  static int usershape_files_open_cnt;
  const char *fn;

  assert(us);
  assert(us->name);
  assert(us->name[0]);

  if (us->f)
    rewind(us->f);
  else {
    if (!(fn = safefile(us->name))) {
      agwarningf("Filename \"%s\" is unsafe\n", us->name);
      return false;
    }
    us->f = gv_fopen(fn, "rb");
    if (us->f == NULL) {
      agwarningf("%s while opening %s\n", strerror(errno), fn);
      return false;
    }
    if (usershape_files_open_cnt >= MAX_USERSHAPE_FILES_OPEN)
      us->nocache = true;
    else
      usershape_files_open_cnt++;
  }
  assert(us->f);
  return true;
}

void gvusershape_file_release(usershape_t *us) {
  if (us->nocache) {
    if (us->f) {
      fclose(us->f);
      us->f = NULL;
    }
  }
}

static void freeUsershape(usershape_t *us) {
  if (us->name)
    agstrfree(0, us->name, false);
  free(us);
}

static usershape_t *gvusershape_open(const char *name) {
  usershape_t *us;

  assert(name);

  if (!ImageDict)
    ImageDict = dtopen(&ImageDictDisc, Dttree);

  if (!(us = gvusershape_find(name))) {
    us = gv_alloc(sizeof(usershape_t));

    us->name = agstrdup(0, name);
    if (!gvusershape_file_access(us)) {
      freeUsershape(us);
      return NULL;
    }

    assert(us->f);

    switch (imagetype(us)) {
    case FT_NULL:
      if (!(us->data = find_user_shape(us->name))) {
        agwarningf(
            "\"%s\" was not found as a file or as a shape library member\n",
            us->name);
        freeUsershape(us);
        return NULL;
      }
      break;
    case FT_GIF:
      gif_size(us);
      break;
    case FT_PNG:
      png_size(us);
      break;
    case FT_BMP:
      bmp_size(us);
      break;
    case FT_JPEG:
      jpeg_size(us);
      break;
    case FT_PS:
      ps_size(us);
      break;
    case FT_WEBP:
      webp_size(us);
      break;
    case FT_SVG:
      svg_size(us);
      break;
    case FT_PDF:
      pdf_size(us);
      break;
    case FT_ICO:
      ico_size(us);
      break;
    case FT_EPS: /* no eps_size code available */
    default:
      break;
    }
    gvusershape_file_release(us);
    dtinsert(ImageDict, us);
    return us;
  }
  gvusershape_file_release(us);
  return us;
}

/* gvusershape_size_dpi:
 * Return image size in points.
 */
point gvusershape_size_dpi(usershape_t *us, pointf dpi) {
  if (!us) {
    return (point){.x = -1, .y = -1};
  }
  if (us->dpi != 0) {
    dpi.x = dpi.y = us->dpi;
  }
  return (point){.x = (int)(us->w * POINTS_PER_INCH / dpi.x),
                 .y = (int)(us->h * POINTS_PER_INCH / dpi.y)};
}

/* gvusershape_size:
 * Loads user image from file name if not already loaded.
 * Return image size in points.
 */
point gvusershape_size(graph_t *g, char *name) {
  pointf dpi;
  static char *oldpath;

  /* no shape file, no shape size */
  if (!name || (*name == '\0')) {
    return (point){.x = -1, .y = -1};
  }

  if (!HTTPServerEnVar && (oldpath != Gvimagepath)) {
    oldpath = Gvimagepath;
    if (ImageDict) {
      dtclose(ImageDict);
      ImageDict = NULL;
    }
  }

  if ((dpi.y = GD_drawing(g)->dpi) >= 1.0)
    dpi.x = dpi.y;
  else
    dpi.x = dpi.y = DEFAULT_DPI;

  usershape_t *const us = gvusershape_open(name);
  return gvusershape_size_dpi(us, dpi);
}
