/// @file
/// @ingroup common_utils
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
#include <common/render.h>
#include <common/htmltable.h>
#include "htmlparse.h"
#include <common/htmllex.h>
#include <cdt/cdt.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <util/alloc.h>
#include <util/gv_ctype.h>
#include <util/startswith.h>
#include <util/strcasecmp.h>
#include <util/strview.h>
#include <util/tokenize.h>
#include <util/unused.h>

#ifdef HAVE_EXPAT
#include <expat.h>
#endif

#ifndef XML_STATUS_ERROR
#define XML_STATUS_ERROR 0
#endif

static unsigned long htmllineno_ctx(htmllexstate_t *ctx);

/* error_context:
 * Print the last 2 "token"s seen.
 */
static void error_context(htmllexstate_t *ctx)
{
  agerr(AGPREV, "... %.*s%.*s ...\n", (int)ctx->prevtok.size,
        ctx->prevtok.data, (int)ctx->currtok.size, ctx->currtok.data);
}

/* htmlerror:
 * yyerror - called by yacc output
 */
void htmlerror(htmlscan_t *scanner, const char *msg)
{
    htmllexstate_t *ctx = &scanner->lexer;
    if (ctx->error)
	return;
    ctx->error = 1;
    agerrorf("%s in line %lu \n", msg, htmllineno(scanner));
    error_context(&scanner->lexer);
}

#ifdef HAVE_EXPAT
/* lexerror:
 * called by lexer when unknown <..> is found.
 */
static void lexerror(htmllexstate_t *ctx, const char *name)
{
    ctx->tok = T_error;
    ctx->error = 1;
    agerrorf("Unknown HTML element <%s> on line %lu \n", name, htmllineno_ctx(ctx));
}

typedef int (*attrFn) (void *, char *);
typedef int (*bcmpfn) (const void *, const void *);

/* Mechanism for automatically processing attributes */
typedef struct {
    char *name;			/* attribute name */
    attrFn action;		/* action to perform if name matches */
} attr_item;

#define ISIZE (sizeof(attr_item))

/* icmp:
 * Compare an attr_item. Used in bsearch
 */
static int icmp(const void *name, const void *item) {
  const attr_item *j = item;
  return strcasecmp(name, j->name);
}

static int bgcolorfn(htmldata_t * p, char *v)
{
    p->bgcolor = strdup(v);
    return 0;
}

static int pencolorfn(htmldata_t * p, char *v)
{
    p->pencolor = strdup(v);
    return 0;
}

static int hreffn(htmldata_t * p, char *v)
{
    p->href = strdup(v);
    return 0;
}

static int sidesfn(htmldata_t * p, char *v)
{
    unsigned short flags = 0; 
    char c;

    while ((c = *v++)) {
	switch (gv_tolower(c)) {
	case 'l' :
	    flags |= BORDER_LEFT;
	    break;
	case 't' :
	    flags |= BORDER_TOP;
	    break;
	case 'r' :
	    flags |= BORDER_RIGHT;
	    break;
	case 'b' :
	    flags |= BORDER_BOTTOM;
	    break;
	default :
	    agwarningf("Unrecognized character '%c' (%d) in sides attribute\n", c, c);
	    break;
	}
    }
    if (flags != BORDER_MASK)
	p->flags |= flags;
    return 0;
}

static int titlefn(htmldata_t * p, char *v)
{
    p->title = strdup(v);
    return 0;
}

static int portfn(htmldata_t * p, char *v)
{
    p->port = strdup(v);
    return 0;
}

#define DELIM " ,"

static int stylefn(htmldata_t * p, char *v)
{
    int rv = 0;
    for (tok_t t = tok(v, DELIM); !tok_end(&t); tok_next(&t)) {
	strview_t tk = tok_get(&t);
	if (strview_case_str_eq(tk, "ROUNDED")) p->style.rounded = true;
	else if (strview_case_str_eq(tk, "RADIAL")) p->style.radial = true;
	else if (strview_case_str_eq(tk,"SOLID")) {
	    p->style.dotted = false;
	    p->style.dashed = false;
	} else if (strview_case_str_eq(tk,"INVISIBLE") ||
	           strview_case_str_eq(tk,"INVIS")) p->style.invisible = true;
	else if (strview_case_str_eq(tk,"DOTTED")) p->style.dotted = true;
	else if (strview_case_str_eq(tk,"DASHED")) p->style.dashed = true;
	else {
	    agwarningf("Illegal value %.*s for STYLE - ignored\n", (int)tk.size,
	          tk.data);
	    rv = 1;
	}
    }
    return rv;
}

static int targetfn(htmldata_t * p, char *v)
{
    p->target = strdup(v);
    return 0;
}

static int idfn(htmldata_t * p, char *v)
{
    p->id = strdup(v);
    return 0;
}


/* doInt:
 * Scan v for integral value. Check that
 * the value is >= min and <= max. Return value in ul.
 * String s is name of value.
 * Return 0 if okay; 1 otherwise.
 */
static int doInt(char *v, char *s, int min, int max, long *ul)
{
    int rv = 0;
    char *ep;
    long b = strtol(v, &ep, 10);

    if (ep == v) {
	agwarningf("Improper %s value %s - ignored", s, v);
	rv = 1;
    } else if (b > max) {
	agwarningf("%s value %s > %d - too large - ignored", s, v, max);
	rv = 1;
    } else if (b < min) {
	agwarningf("%s value %s < %d - too small - ignored", s, v, min);
	rv = 1;
    } else
	*ul = b;
    return rv;
}


static int gradientanglefn(htmldata_t * p, char *v)
{
    long u;

    if (doInt(v, "GRADIENTANGLE", 0, 360, &u))
	return 1;
    p->gradientangle = (unsigned short) u;
    return 0;
}


static int borderfn(htmldata_t * p, char *v)
{
    long u;

    if (doInt(v, "BORDER", 0, UCHAR_MAX, &u))
	return 1;
    p->border = (unsigned char) u;
    p->flags |= BORDER_SET;
    return 0;
}

static int cellpaddingfn(htmldata_t * p, char *v)
{
    long u;

    if (doInt(v, "CELLPADDING", 0, UCHAR_MAX, &u))
	return 1;
    p->pad = (unsigned char) u;
    p->flags |= PAD_SET;
    return 0;
}

static int cellspacingfn(htmldata_t * p, char *v)
{
    long u;

    if (doInt(v, "CELLSPACING", SCHAR_MIN, SCHAR_MAX, &u))
	return 1;
    p->space = (signed char) u;
    p->flags |= SPACE_SET;
    return 0;
}

static int cellborderfn(htmltbl_t * p, char *v)
{
    long u;

    if (doInt(v, "CELLBORDER", 0, INT8_MAX, &u))
	return 1;
    p->cellborder = (int8_t)u;
    return 0;
}

static int columnsfn(htmltbl_t * p, char *v)
{
    if (*v != '*') {
	agwarningf("Unknown value %s for COLUMNS - ignored\n", v);
	return 1;
    }
    p->vrule = true;
    return 0;
}

static int rowsfn(htmltbl_t * p, char *v)
{
    if (*v != '*') {
	agwarningf("Unknown value %s for ROWS - ignored\n", v);
	return 1;
    }
    p->hrule = true;
    return 0;
}

static int fixedsizefn(htmldata_t * p, char *v)
{
    int rv = 0;
    if (!strcasecmp(v, "TRUE"))
	p->flags |= FIXED_FLAG;
    else if (strcasecmp(v, "FALSE")) {
	agwarningf("Illegal value %s for FIXEDSIZE - ignored\n", v);
	rv = 1;
    }
    return rv;
}

static int valignfn(htmldata_t * p, char *v)
{
    int rv = 0;
    if (!strcasecmp(v, "BOTTOM"))
	p->flags |= VALIGN_BOTTOM;
    else if (!strcasecmp(v, "TOP"))
	p->flags |= VALIGN_TOP;
    else if (strcasecmp(v, "MIDDLE")) {
	agwarningf("Illegal value %s for VALIGN - ignored\n", v);
	rv = 1;
    }
    return rv;
}

static int halignfn(htmldata_t * p, char *v)
{
    int rv = 0;
    if (!strcasecmp(v, "LEFT"))
	p->flags |= HALIGN_LEFT;
    else if (!strcasecmp(v, "RIGHT"))
	p->flags |= HALIGN_RIGHT;
    else if (strcasecmp(v, "CENTER")) {
	agwarningf("Illegal value %s for ALIGN - ignored\n", v);
	rv = 1;
    }
    return rv;
}

static int cell_halignfn(htmldata_t * p, char *v)
{
    int rv = 0;
    if (!strcasecmp(v, "LEFT"))
	p->flags |= HALIGN_LEFT;
    else if (!strcasecmp(v, "RIGHT"))
	p->flags |= HALIGN_RIGHT;
    else if (!strcasecmp(v, "TEXT"))
	p->flags |= HALIGN_TEXT;
    else if (strcasecmp(v, "CENTER"))
	rv = 1;
    if (rv)
	agwarningf("Illegal value %s for ALIGN in TD - ignored\n", v);
    return rv;
}

static int balignfn(htmldata_t * p, char *v)
{
    int rv = 0;
    if (!strcasecmp(v, "LEFT"))
	p->flags |= BALIGN_LEFT;
    else if (!strcasecmp(v, "RIGHT"))
	p->flags |= BALIGN_RIGHT;
    else if (strcasecmp(v, "CENTER"))
	rv = 1;
    if (rv)
	agwarningf("Illegal value %s for BALIGN in TD - ignored\n", v);
    return rv;
}

static int heightfn(htmldata_t * p, char *v)
{
    long u;

    if (doInt(v, "HEIGHT", 0, USHRT_MAX, &u))
	return 1;
    p->height = (unsigned short) u;
    return 0;
}

static int widthfn(htmldata_t * p, char *v)
{
    long u;

    if (doInt(v, "WIDTH", 0, USHRT_MAX, &u))
	return 1;
    p->width = (unsigned short) u;
    return 0;
}

static int rowspanfn(htmlcell_t * p, char *v)
{
    long u;

    if (doInt(v, "ROWSPAN", 0, UINT16_MAX, &u))
	return 1;
    if (u == 0) {
	agwarningf("ROWSPAN value cannot be 0 - ignored\n");
	return 1;
    }
    p->rowspan = (uint16_t)u;
    return 0;
}

static int colspanfn(htmlcell_t * p, char *v)
{
    long u;

    if (doInt(v, "COLSPAN", 0, UINT16_MAX, &u))
	return 1;
    if (u == 0) {
	agwarningf("COLSPAN value cannot be 0 - ignored\n");
	return 1;
    }
    p->colspan = (uint16_t)u;
    return 0;
}

static int fontcolorfn(textfont_t * p, char *v)
{
    p->color = v;
    return 0;
}

static int facefn(textfont_t * p, char *v)
{
    p->name = v;
    return 0;
}

static int ptsizefn(textfont_t * p, char *v)
{
    long u;

    if (doInt(v, "POINT-SIZE", 0, UCHAR_MAX, &u))
	return 1;
    p->size = (double) u;
    return 0;
}

static int srcfn(htmlimg_t * p, char *v)
{
    p->src = strdup(v);
    return 0;
}

static int scalefn(htmlimg_t * p, char *v)
{
    p->scale = strdup(v);
    return 0;
}

static int alignfn(int *p, char *v)
{
    int rv = 0;
    if (!strcasecmp(v, "RIGHT"))
	*p = 'r';
    else if (!strcasecmp(v, "LEFT"))
	*p = 'l';
    else if (!strcasecmp(v, "CENTER"))
	*p = 'n';
    else {
	agwarningf("Illegal value %s for ALIGN - ignored\n", v);
	rv = 1;
    }
    return rv;
}

/* Tables used in binary search; MUST be alphabetized */
static attr_item tbl_items[] = {
    {"align", (attrFn) halignfn},
    {"bgcolor", (attrFn) bgcolorfn},
    {"border", (attrFn) borderfn},
    {"cellborder", (attrFn) cellborderfn},
    {"cellpadding", (attrFn) cellpaddingfn},
    {"cellspacing", (attrFn) cellspacingfn},
    {"color", (attrFn) pencolorfn},
    {"columns", (attrFn) columnsfn},
    {"fixedsize", (attrFn) fixedsizefn},
    {"gradientangle", (attrFn) gradientanglefn},
    {"height", (attrFn) heightfn},
    {"href", (attrFn) hreffn},
    {"id", (attrFn) idfn},
    {"port", (attrFn) portfn},
    {"rows", (attrFn) rowsfn},
    {"sides", (attrFn) sidesfn},
    {"style", (attrFn) stylefn},
    {"target", (attrFn) targetfn},
    {"title", (attrFn) titlefn},
    {"tooltip", (attrFn) titlefn},
    {"valign", (attrFn) valignfn},
    {"width", (attrFn) widthfn},
};

static attr_item cell_items[] = {
    {"align", (attrFn) cell_halignfn},
    {"balign", (attrFn) balignfn},
    {"bgcolor", (attrFn) bgcolorfn},
    {"border", (attrFn) borderfn},
    {"cellpadding", (attrFn) cellpaddingfn},
    {"cellspacing", (attrFn) cellspacingfn},
    {"color", (attrFn) pencolorfn},
    {"colspan", (attrFn) colspanfn},
    {"fixedsize", (attrFn) fixedsizefn},
    {"gradientangle", (attrFn) gradientanglefn},
    {"height", (attrFn) heightfn},
    {"href", (attrFn) hreffn},
    {"id", (attrFn) idfn},
    {"port", (attrFn) portfn},
    {"rowspan", (attrFn) rowspanfn},
    {"sides", (attrFn) sidesfn},
    {"style", (attrFn) stylefn},
    {"target", (attrFn) targetfn},
    {"title", (attrFn) titlefn},
    {"tooltip", (attrFn) titlefn},
    {"valign", (attrFn) valignfn},
    {"width", (attrFn) widthfn},
};

static attr_item font_items[] = {
    {"color", (attrFn) fontcolorfn},
    {"face", (attrFn) facefn},
    {"point-size", (attrFn) ptsizefn},
};

static attr_item img_items[] = {
    {"scale", (attrFn) scalefn},
    {"src", (attrFn) srcfn},
};

static attr_item br_items[] = {
    {"align", (attrFn) alignfn},
};

/* doAttrs:
 * General function for processing list of name/value attributes.
 * Do binary search on items table. If match found, invoke action
 * passing it tp and attribute value.
 * Table size is given by nel
 * Name/value pairs are in array atts, which is null terminated.
 * s is the name of the HTML element being processed.
 */
static void doAttrs(htmllexstate_t *ctx, void *tp, attr_item *items, size_t nel, char **atts,
                    char *s) {
    char *name;
    char *val;
    attr_item *ip;

    while ((name = *atts++) != NULL) {
	val = *atts++;
	ip = bsearch(name, items, nel, ISIZE, icmp);
	if (ip)
	    ctx->warn |= ip->action(tp, val);
	else {
	    agwarningf("Illegal attribute %s in %s - ignored\n", name,
		  s);
	    ctx->warn = 1;
	}
    }
}

static void mkBR(htmllexstate_t *ctx, char **atts)
{
    ctx->htmllval->i = UNSET_ALIGN;
    doAttrs(ctx, &ctx->htmllval->i, br_items, sizeof(br_items) / ISIZE, atts, "<BR>");
}

static htmlimg_t *mkImg(htmllexstate_t *ctx, char **atts)
{
    htmlimg_t *img = gv_alloc(sizeof(htmlimg_t));

    doAttrs(ctx, img, img_items, sizeof(img_items) / ISIZE, atts, "<IMG>");

    return img;
}

static textfont_t *mkFont(htmllexstate_t *ctx, char **atts, unsigned char flags) {
    textfont_t tf = {NULL,NULL,NULL,0.0,0,0};

    tf.size = -1.0;		/* unassigned */
    enum { FLAGS_MAX = (1 << GV_TEXTFONT_FLAGS_WIDTH) - 1  };
    assert(flags <= FLAGS_MAX);
    tf.flags = (unsigned char)(flags & FLAGS_MAX);
    if (atts)
	doAttrs(ctx, &tf, font_items, sizeof(font_items) / ISIZE, atts, "<FONT>");

    return dtinsert(ctx->gvc->textfont_dt, &tf);
}

static htmlcell_t *mkCell(htmllexstate_t *ctx, char **atts)
{
    htmlcell_t *cell = gv_alloc(sizeof(htmlcell_t));

    cell->colspan = 1;
    cell->rowspan = 1;
    doAttrs(ctx, cell, cell_items, sizeof(cell_items) / ISIZE, atts, "<TD>");

    return cell;
}

static htmltbl_t *mkTbl(htmllexstate_t *ctx, char **atts)
{
    htmltbl_t *tbl = gv_alloc(sizeof(htmltbl_t));

    tbl->row_count = SIZE_MAX; // flag that table is a raw, parsed table
    tbl->rows = (rows_t){.dtor = free_ritem};
    tbl->cellborder = -1; // unset cell border attribute
    doAttrs(ctx, tbl, tbl_items, sizeof(tbl_items) / ISIZE, atts, "<TABLE>");

    return tbl;
}

static void startElement(void *user, const char *name, char **atts)
{
    htmllexstate_t *ctx = user;

    if (strcasecmp(name, "TABLE") == 0) {
	ctx->htmllval->tbl = mkTbl(ctx, atts);
	ctx->inCell = 0;
	ctx->tok = T_table;
    } else if (strcasecmp(name, "TR") == 0 || strcasecmp(name, "TH") == 0) {
	ctx->inCell = 0;
	ctx->tok = T_row;
    } else if (strcasecmp(name, "TD") == 0) {
	ctx->inCell = 1;
	ctx->htmllval->cell = mkCell(ctx, atts);
	ctx->tok = T_cell;
    } else if (strcasecmp(name, "FONT") == 0) {
	ctx->htmllval->font = mkFont(ctx, atts, 0);
	ctx->tok = T_font;
    } else if (strcasecmp(name, "B") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_BF);
	ctx->tok = T_bold;
    } else if (strcasecmp(name, "S") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_S);
	ctx->tok = T_s;
    } else if (strcasecmp(name, "U") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_UL);
	ctx->tok = T_underline;
    } else if (strcasecmp(name, "O") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_OL);
	ctx->tok = T_overline;
    } else if (strcasecmp(name, "I") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_IF);
	ctx->tok = T_italic;
    } else if (strcasecmp(name, "SUP") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_SUP);
	ctx->tok = T_sup;
    } else if (strcasecmp(name, "SUB") == 0) {
	ctx->htmllval->font = mkFont(ctx, 0, HTML_SUB);
	ctx->tok = T_sub;
    } else if (strcasecmp(name, "BR") == 0) {
	mkBR(ctx, atts);
	ctx->tok = T_br;
    } else if (strcasecmp(name, "HR") == 0) {
	ctx->tok = T_hr;
    } else if (strcasecmp(name, "VR") == 0) {
	ctx->tok = T_vr;
    } else if (strcasecmp(name, "IMG") == 0) {
	ctx->htmllval->img = mkImg(ctx, atts);
	ctx->tok = T_img;
    } else if (strcasecmp(name, "HTML") == 0) {
	ctx->tok = T_html;
    } else {
	lexerror(ctx, name);
    }
}

static void endElement(void *user, const char *name)
{
    htmllexstate_t *ctx = user;

    if (strcasecmp(name, "TABLE") == 0) {
	ctx->tok = T_end_table;
	ctx->inCell = 1;
    } else if (strcasecmp(name, "TR") == 0 || strcasecmp(name, "TH") == 0) {
	ctx->tok = T_end_row;
    } else if (strcasecmp(name, "TD") == 0) {
	ctx->tok = T_end_cell;
	ctx->inCell = 0;
    } else if (strcasecmp(name, "HTML") == 0) {
	ctx->tok = T_end_html;
    } else if (strcasecmp(name, "FONT") == 0) {
	ctx->tok = T_end_font;
    } else if (strcasecmp(name, "B") == 0) {
	ctx->tok = T_n_bold;
    } else if (strcasecmp(name, "U") == 0) {
	ctx->tok = T_n_underline;
    } else if (strcasecmp(name, "O") == 0) {
	ctx->tok = T_n_overline;
    } else if (strcasecmp(name, "I") == 0) {
	ctx->tok = T_n_italic;
    } else if (strcasecmp(name, "SUP") == 0) {
	ctx->tok = T_n_sup;
    } else if (strcasecmp(name, "SUB") == 0) {
	ctx->tok = T_n_sub;
    } else if (strcasecmp(name, "S") == 0) {
	ctx->tok = T_n_s;
    } else if (strcasecmp(name, "BR") == 0) {
	if (ctx->tok == T_br)
	    ctx->tok = T_BR;
	else
	    ctx->tok = T_end_br;
    } else if (strcasecmp(name, "HR") == 0) {
	if (ctx->tok == T_hr)
	    ctx->tok = T_HR;
	else
	    ctx->tok = T_end_hr;
    } else if (strcasecmp(name, "VR") == 0) {
	if (ctx->tok == T_vr)
	    ctx->tok = T_VR;
	else
	    ctx->tok = T_end_vr;
    } else if (strcasecmp(name, "IMG") == 0) {
	if (ctx->tok == T_img)
	    ctx->tok = T_IMG;
	else
	    ctx->tok = T_end_img;
    } else {
	lexerror(ctx, name);
    }
}

/* characterData:
 * Generate T_string token. Do this only when immediately in
 * <TD>..</TD> or <HTML>..</HTML>, i.e., when inCell is true.
 * Strip out formatting characters but keep spaces.
 * Distinguish between all whitespace vs. strings with non-whitespace
 * characters.
 */
static void characterData(void *user, const char *s, int length)
{
    htmllexstate_t *ctx = user;

    int i, cnt = 0;
    unsigned char c;

    if (ctx->inCell) {
	for (i = length; i; i--) {
	    c = *s++;
	    if (c >= ' ') {
		cnt++;
		agxbputc(ctx->xb, (char)c);
	    }
	}
	if (cnt) ctx->tok = T_string;
    }
}
#endif

int initHTMLlexer(htmlscan_t *scanner, char *src, agxbuf * xb, htmlenv_t *env)
{
#ifdef HAVE_EXPAT
    htmllexstate_t *ctx = &scanner->lexer;

    ctx->xb = xb;
    ctx->lb = (agxbuf){0};
    ctx->ptr = src;
    ctx->mode = 0;
    ctx->warn = 0;
    ctx->error = 0;
    ctx->currtok = (strview_t){0};
    ctx->prevtok = (strview_t){0};
    ctx->inCell = 1;
    ctx->parser = XML_ParserCreate(charsetToStr(GD_charset(env->g)));
    ctx->gvc = GD_gvc(env->g);
    XML_SetUserData(ctx->parser, ctx);
    XML_SetElementHandler(ctx->parser,
			  (XML_StartElementHandler) startElement,
			  endElement);
    XML_SetCharacterDataHandler(ctx->parser, characterData);
    return 0;
#else
    (void)scanner;
    (void)src;
    (void)xb;
    (void)env;

    static atomic_flag first;
    if (!atomic_flag_test_and_set(&first)) {
	agwarningf(
	      "Not built with libexpat. Table formatting is not available.\n");
    }
    return 1;
#endif
}

int clearHTMLlexer(htmlscan_t *scanner)
{
#ifdef HAVE_EXPAT
    htmllexstate_t *ctx = &scanner->lexer;
    int rv = ctx->error ? 3 : ctx->warn;
    XML_ParserFree(ctx->parser);
    agxbfree (&ctx->lb);
    return rv;
#else
    (void)scanner;

    return 1;
#endif
}

/// \p agxbput, but assume that source and destination may overlap
static UNUSED void agxbput_move(agxbuf *dst, const char *src) {
  // we cannot call `agxbput` itself because it calls `memcpy`, thereby
  // implicitly assuming that source and destination do not overlap
  char *src_copy = gv_strdup(src);
  agxbput(dst, src_copy);
  free(src_copy);
}

#ifdef HAVE_EXPAT
/* eatComment:
 * Given first character after open comment, eat characters
 * up to comment close, returning pointer to closing > if it exists,
 * or null character otherwise.
 * We rely on HTML strings having matched nested <>.
 */
static char *eatComment(htmllexstate_t *ctx, char *p)
{
    int depth = 1;
    char *s = p;
    char c;

    while (depth && (c = *s++)) {
	if (c == '<')
	    depth++;
	else if (c == '>')
	    depth--;
    }
    s--;			/* move back to '\0' or '>' */
    if (*s) {
	char *t = s - 2;
	if (t < p || !startswith(t, "--")) {
	    agwarningf("Unclosed comment\n");
	    ctx->warn = 1;
	}
    }
    return s;
}

/* findNext:
 * Return next XML unit. This is either <..>, an HTML 
 * comment <!-- ... -->, or characters up to next <.
 */
static char *findNext(htmllexstate_t *ctx, char *s, agxbuf* xb)
{
    char* t = s + 1;
    char c;

    if (*s == '<') {
	if (startswith(t, "!--"))
	    t = eatComment(ctx, t + 3);
	else
	    while (*t && *t != '>')
		t++;
	if (*t != '>') {
	    agwarningf("Label closed before end of HTML element\n");
	    ctx->warn = 1;
	} else
	    t++;
    } else {
	t = s;
	while ((c = *t) && c != '<') {
	    if (c == '&' && *(t+1) != '#') {
		t = scanEntity(t + 1, xb);
	    }
	    else {
		agxbputc(xb, c);
		t++;
	    }
	}
    }
    return t;
}

/** guard a trailing right square bracket (]) from misinterpretation
 *
 * When parsing in incremental mode, expat tries to recognize malformed CDATA
 * section terminators. See XML_TOK_TRAILING_RSQB in the expat source. As a
 * result, when seeing text that ends in a ']' expat buffers this internally and
 * returns truncated text for the current parse. The ']' is flushed as part of
 * the next parsing call when expat learns it is not a CDATA section terminator.
 *
 * To prevent this situation from occurring, this function turns any trailing
 * ']' into its XML escape sequence. This causes expat to realize immediately it
 * is not part of a CDATA section terminator and flush it in the first parsing
 * call. This has no effect on the final output, because expat handles the
 * translation back from this escape sequence to ']'.
 *
 * @param xb Buffer containing content to protect
 */
static void protect_rsqb(agxbuf *xb) {

  // if the buffer is empty, we have nothing to do
  if (agxblen(xb) == 0) {
    return;
  }

  // check the last character and if it is not ], we have nothing to do
  char *data = agxbuse(xb);
  size_t size = strlen(data);
  assert(size > 0);
  if (data[size - 1] != ']') {
    agxbput_move(xb, data);
    return;
  }

  // truncate ] and write back the remaining prefix
  data[size - 1] = '\0';
  agxbput_move(xb, data);

  // write an XML-escaped version of ] as a replacement
  agxbput(xb, "&#93;");
}
#endif


unsigned long htmllineno(htmlscan_t *scanner) {
    return htmllineno_ctx(&scanner->lexer);
}

static unsigned long htmllineno_ctx(htmllexstate_t *ctx) {
#ifdef HAVE_EXPAT
    return XML_GetCurrentLineNumber(ctx->parser);
#else
    (void)ctx;

    return 0;
#endif
}

#ifdef DEBUG
static void printTok(htmllexstate_t *ctx, int tok)
{
    char *s;

    switch (tok) {
    case T_end_br:
	s = "T_end_br";
	break;
    case T_end_img:
	s = "T_end_img";
	break;
    case T_row:
	s = "T_row";
	break;
    case T_end_row:
	s = "T_end_row";
	break;
    case T_html:
	s = "T_html";
	break;
    case T_end_html:
	s = "T_end_html";
	break;
    case T_end_table:
	s = "T_end_table";
	break;
    case T_end_cell:
	s = "T_end_cell";
	break;
    case T_end_font:
	s = "T_end_font";
	break;
    case T_string:
	s = "T_string";
	break;
    case T_error:
	s = "T_error";
	break;
    case T_n_italic:
	s = "T_n_italic";
	break;
    case T_n_bold:
	s = "T_n_bold";
	break;
    case T_n_underline:
	s = "T_n_underline";
	break;
    case T_n_overline:
	s = "T_n_overline";
	break;
    case T_n_sup:
	s = "T_n_sup";
	break;
    case T_n_sub:
	s = "T_n_sub";
	break;
    case T_n_s:
	s = "T_n_s";
	break;
    case T_HR:
	s = "T_HR";
	break;
    case T_hr:
	s = "T_hr";
	break;
    case T_end_hr:
	s = "T_end_hr";
	break;
    case T_VR:
	s = "T_VR";
	break;
    case T_vr:
	s = "T_vr";
	break;
    case T_end_vr:
	s = "T_end_vr";
	break;
    case T_BR:
	s = "T_BR";
	break;
    case T_br:
	s = "T_br";
	break;
    case T_IMG:
	s = "T_IMG";
	break;
    case T_img:
	s = "T_img";
	break;
    case T_table:
	s = "T_table";
	break;
    case T_cell:
	s = "T_cell";
	break;
    case T_font:
	s = "T_font";
	break;
    case T_italic:
	s = "T_italic";
	break;
    case T_bold:
	s = "T_bold";
	break;
    case T_underline:
	s = "T_underline";
	break;
    case T_overline:
	s = "T_overline";
	break;
    case T_sup:
	s = "T_sup";
	break;
    case T_sub:
	s = "T_sub";
	break;
    case T_s:
	s = "T_s";
	break;
    default:
	s = "<unknown>";
    }
    if (tok == T_string) {
	const char *token_text = agxbuse(ctx->xb);
	fprintf(stderr, "%s \"%s\"\n", s, token_text);
	agxbput_move(ctx->xb, token_text);
    } else
	fprintf(stderr, "%s\n", s);
}

#endif

int htmllex(union HTMLSTYPE *htmllval, htmlscan_t *scanner)
{
#ifdef HAVE_EXPAT
    static char *begin_html = "<HTML>";
    static char *end_html = "</HTML>";

    char *s;
    char *endp = 0;
    size_t len, llen;
    int rv;
    htmllexstate_t *ctx = &scanner->lexer;

    ctx->htmllval = htmllval;
    ctx->tok = 0;
    do {
	if (ctx->mode == 2)
	    return EOF;
	if (ctx->mode == 0) {
	    ctx->mode = 1;
	    s = begin_html;
	    len = strlen(s);
	    endp = 0;
	} else {
	    s = ctx->ptr;
	    if (*s == '\0') {
		ctx->mode = 2;
		s = end_html;
		len = strlen(s);
	    } else {
		endp = findNext(ctx, s,&ctx->lb);
		len = (size_t)(endp - s);
	    }
	}

	protect_rsqb(&ctx->lb);

	ctx->prevtok = ctx->currtok;
	ctx->currtok = (strview_t){.data = s, .size = len};
	if ((llen = agxblen(&ctx->lb))) {
	    assert(llen <= INT_MAX && "XML token too long for expat API");
	    rv = XML_Parse(ctx->parser, agxbuse(&ctx->lb), (int)llen, 0);
	} else {
	    assert(len <= INT_MAX && "XML token too long for expat API");
	    rv = XML_Parse(ctx->parser, s, (int)len, len ? 0 : 1);
	}
	if (rv == XML_STATUS_ERROR) {
	    if (!ctx->error) {
		agerrorf("%s in line %lu \n",
		      XML_ErrorString(XML_GetErrorCode(ctx->parser)), htmllineno(scanner));
		error_context(ctx);
		ctx->error = 1;
		ctx->tok = T_error;
	    }
	}
	if (endp)
	    ctx->ptr = endp;
    } while (ctx->tok == 0);
#ifdef DEBUG
    printTok (ctx, ctx->tok);
#endif
    return ctx->tok;
#else
    (void)htmllval;
    (void)scanner;

    return EOF;
#endif
}

