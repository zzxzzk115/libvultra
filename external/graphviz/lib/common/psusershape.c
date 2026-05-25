/// @file
/// @ingroup common_render
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

#include <sys/stat.h>
#include <common/render.h>
#include <gvc/gvio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <util/alloc.h>
#include <util/gv_fopen.h>
#include <util/strcasecmp.h>

static int N_EPSF_files;
static Dict_t *EPSF_contents;

static void ps_image_free(void *shape) {
    usershape_t *p = shape;
    free(p->data);
}

static Dtdisc_t ImageDictDisc = {
    .key = offsetof(usershape_t, name),
    .size = -1,
    .freef = ps_image_free,
};

static usershape_t *user_init(const char *str)
{
    char line[BUFSIZ];
    FILE *fp;
    struct stat statbuf;
    int lx, ly, ux, uy;

    if (!EPSF_contents)
	EPSF_contents = dtopen(&ImageDictDisc, Dtoset);

    usershape_t *us = dtmatch(EPSF_contents, str);
    if (us)
	return us;

    if (!(fp = gv_fopen(str, "r"))) {
	agwarningf("couldn't open epsf file %s\n", str);
	return NULL;
    }
    /* try to find size */
    bool saw_bb = false;
    bool must_inline = false;
    while (fgets(line, sizeof(line), fp)) {
	if (sscanf
	    (line, "%%%%BoundingBox: %d %d %d %d", &lx, &ly, &ux, &uy) == 4) {
	    saw_bb = true;
	}
	if (line[0] != '%' && strstr(line,"read")) must_inline = true;
	if (saw_bb && must_inline) break;
    }

    if (saw_bb) {
	us = gv_alloc(sizeof(usershape_t));
	us->x = lx;
	us->y = ly;
	us->w = ux - lx;
	us->h = uy - ly;
	us->name = str;
	us->macro_id = N_EPSF_files++;
	fstat(fileno(fp), &statbuf);
	char *contents = us->data = gv_calloc((size_t)statbuf.st_size + 1, sizeof(char));
	rewind(fp);
	size_t rc = fread(contents, (size_t)statbuf.st_size, 1, fp);
	if (rc == 1) {
            contents[statbuf.st_size] = '\0';
            dtinsert(EPSF_contents, us);
            us->must_inline = must_inline;
        }
        else {
            agwarningf("couldn't read from epsf file %s\n", str);
            free(us->data);
            free(us);
            us = NULL;
        }
    } else {
	agwarningf("BoundingBox not found in epsf file %s\n", str);
	us = NULL;
    }
    fclose(fp);
    return us;
}

void epsf_init(node_t * n)
{
    epsf_t *desc;
    const char *str;

    if ((str = safefile(agget(n, "shapefile")))) {
	usershape_t *us = user_init(str);
	if (!us)
	    return;
	const double dx = us->w;
	const double dy = us->h;
	ND_width(n) = PS2INCH(dx);
	ND_height(n) = PS2INCH(dy);
	ND_shape_info(n) = desc = gv_alloc(sizeof(epsf_t));
	desc->macro_id = us->macro_id;
	desc->offset.x = -us->x - dx / 2;
	desc->offset.y = -us->y - dy / 2;
    } else
	agwarningf("shapefile not set or not found for epsf node %s\n", agnameof(n));
}

void epsf_free(node_t * n)
{

    free(ND_shape_info(n));
}


/* cat_libfile:
 * Write library files onto the given file pointer.
 * arglib is an NULL-terminated array of char*
 * Each non-trivial entry should be the name of a file to be included.
 * stdlib is an NULL-terminated array of char*
 * Each of these is a line of a standard library to be included.
 * If any item in arglib is the empty string, the stdlib is not used.
 * The stdlib is printed first, if used, followed by the user libraries.
 * We check that for web-safe file usage.
 */
void cat_libfile(GVJ_t * job, const char **arglib, const char **stdlib)
{
    FILE *fp;
    const char *p;
    bool use_stdlib = true;

    /* check for empty string to turn off stdlib */
    if (arglib) {
        for (int i = 0; use_stdlib && (p = arglib[i]); i++) {
            if (*p == '\0')
                use_stdlib = false;
        }
    }
    if (use_stdlib)
        for (const char **s = stdlib; *s; s++) {
            gvputs(job, *s);
            gvputs(job, "\n");
        }
    if (arglib) {
        for (int i = 0; (p = arglib[i]) != 0; i++) {
            if (*p == '\0')
                continue;       /* ignore empty string */
            const char *safepath = safefile(p);    /* make sure filename is okay */
	    if (!safepath) {
		agwarningf("can't find library file %s\n", p);
	    }
            else if ((fp = gv_fopen(safepath, "r"))) {
                while (true) {
                    char bp[BUFSIZ] = {0};
                    size_t r = fread(bp, 1, sizeof(bp), fp);
                    gvwrite(job, bp, r);
                    if (r < sizeof(bp)) {
                      break;
                    }
                }
                gvputs(job, "\n"); /* append a newline just in case */
		fclose (fp);
            } else
                agwarningf("can't open library file %s\n", safepath);
        }
    }
}

/* this removes EPSF DSC comments that, when nested in another
 * document, cause errors in Ghostview and other Postscript
 * processors (although legal according to the Adobe EPSF spec).
 *
 * N.B. PostScript lines can end with \n, \r or \r\n.
 */
void epsf_emit_body(GVJ_t *job, usershape_t *us)
{
    char *p = us->data;
    while (*p) {
	/* skip %%EOF lines */
	if (!strncasecmp(p, "%%EOF", 5) || !strncasecmp(p, "%%BEGIN", 7) ||
	    !strncasecmp(p, "%%END", 5) || !strncasecmp(p, "%%TRAILER", 9)) {
	    /* check for *p since last line might not end in '\n' */
	    while (*p != '\0' && *p != '\r' && *p != '\n') p++;
	    if (*p == '\r' && p[1] == '\n') p += 2;
	    else if (*p) p++;
	    continue;
	}
	/* output line */
	while (*p != '\0' && *p != '\r' && *p != '\n') {
	    gvputc(job, *p);
	    p++;
	}
	if (*p == '\r' && p[1] == '\n') p += 2;
	else if (*p) p++;
	gvputc(job, '\n');
    }
}

void epsf_define(GVJ_t *job)
{
    if (!EPSF_contents)
	return;
    for (usershape_t *us = dtfirst(EPSF_contents); us; us = dtnext(EPSF_contents, us)) {
	if (us->must_inline)
	    continue;
	gvprintf(job, "/user_shape_%d {\n", us->macro_id);
	gvputs(job, "%%BeginDocument:\n");
	epsf_emit_body(job, us);
	gvputs(job, "%%EndDocument\n");
	gvputs(job, "} bind def\n");
    }
}

enum {ASCII, LATIN1, NONLATIN};

/* charsetOf:
 * Assuming legal utf-8 input, determine if
 * the character value range is ascii, latin-1 or otherwise.
 */
static int
charsetOf (char* s)
{
    int r = ASCII;
    unsigned char c;

    while ((c = *(unsigned char*)s++)) {
	if (c < 0x7F) 
	    continue;
	else if ((c & 0xFC) == 0xC0) {
	    r = LATIN1;
	    s++; /* eat second byte */
	}
	else return NONLATIN;
    }
    return r;
}

char *ps_string(char *ins, int chset)
{
    char *base;
    static atomic_flag warned;

    switch (chset) {
    case CHAR_UTF8 :
        base = ins;
	break;
    case CHAR_LATIN1 :
        base = utf8ToLatin1 (ins);
	break;
    default :
	switch (charsetOf (ins)) {
	case ASCII :
	    base = ins;
	    break;
	case LATIN1 :
	    base = utf8ToLatin1 (ins);
	    break;
	case NONLATIN :
	    if (!atomic_flag_test_and_set(&warned)) {
		agwarningf("UTF-8 input uses non-Latin1 characters which cannot be handled by this PostScript driver\n");
	    }
	    base = ins;
	    break;
	default:
	    base = ins;
	    break;
	}
    }

    agxbuf xb = {0};
    agxbputc (&xb, LPAREN);
    char *s = base;
    while (*s) {
        if (*s == LPAREN || *s == RPAREN || *s == '\\')
            agxbputc (&xb, '\\');
        agxbputc (&xb, *s++);
    }
    agxbputc (&xb, RPAREN);
    if (base != ins) free (base);
    return agxbdisown(&xb);
}
