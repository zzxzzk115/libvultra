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

#include <cgraph/agstrcanon.h>
#include <common/render.h>
#include <gvc/gvc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <util/agxbuf.h>
#include <util/prisize_t.h>

/// state for offset calculations
typedef struct {
  double Y;  ///< ymin + ymax
  double YF; ///< `Y` in inches
} offsets_t;

static double YFDIR(offsets_t offsets, double y) {
  return Y_invert ? offsets.YF - y : y;
}

double yDir(double y, double Y_off) {
  return Y_invert ? Y_off - y : y;
}

static void agputs(int (*putstr)(void *chan, const char *str), const char* s,
                   void* fp) {
    putstr(fp, s);
}

static void agputc(int (*putstr)(void *chan, const char *str), char c,
                   void *fp) {
    char buf[] = {c, '\0'};
    putstr(fp, buf);
}

static void printstring(int (*putstr)(void *chan, const char *str), void *f,
                        char *prefix, char *s) {
  if (prefix) agputs(putstr, prefix, f);
  agputs(putstr, s, f);
}

static void printint(int (*putstr)(void *chan, const char *str), void *f,
                     char *prefix, size_t i) {
  agxbuf buf = {0};
    
  if (prefix) agputs(putstr, prefix, f);
  agxbprint(&buf, "%" PRISIZE_T, i);
  agputs(putstr, agxbuse(&buf), f);
  agxbfree(&buf);
}

static void printdouble(int (*putstr)(void *chan, const char *str), void *f,
                        char *prefix, double v) {
  agxbuf buf = {0};
    
  if (prefix) agputs(putstr, prefix, f);
  agxbprint(&buf, "%.5g", v);
  agputs(putstr, agxbuse(&buf), f);
  agxbfree(&buf);
}

static void printpoint(int (*putstr)(void *chan, const char *str), void *f,
                       pointf p, double yOff) {
  printdouble(putstr, f, " ", PS2INCH(p.x));
  printdouble(putstr, f, " ", PS2INCH(yDir(p.y, yOff)));
}

/* setYInvert:
 * Set parameters used to flip coordinate system (y=0 at top).
 * Values do not need to be unset, since if Y_invert is set, it's
 * set for * all graphs during current run, so each will 
 * reinitialize the values for its bbox.
 */
static offsets_t setYInvert(graph_t *g) {
    offsets_t rv = {0};
    if (Y_invert) {
	rv.Y = GD_bb(g).UR.y + GD_bb(g).LL.y;
	rv.YF = PS2INCH(rv.Y);
    }
    return rv;
}

/* canon:
 * Canonicalize a string which may not have been allocated using agstrdup.
 *
 * @param buffer Scratch space to use during canonicalization. This must be at
 *   least `agstrcanon_bytes(s)` bytes.
 */
static char *canon(graph_t *g, char *s, char *buffer) {
    char* ns = agstrdup (g, s);
    char *const cs = agstrcanon(ns, buffer);
    agstrfree(g, ns, false);
    return cs;
}

static void writenodeandport(int (*putstr)(void *chan, const char *str),
                             FILE *f, node_t *node, char *portname) {
    char *name;
    char *const value =
      IS_CLUST_NODE(node) ? strchr(agnameof(node), ':') + 1 : agnameof(node);
    char *const buffer = agstrcanon_buffer(value);
    if (IS_CLUST_NODE(node)) {
	name = canon(agraphof(node), value, buffer);
    } else
	name = agstrcanon(value, buffer);
    printstring(putstr, f, " ", name); /* slimey i know */
    free(buffer);
    if (portname && *portname) {
	char *const buffer2 = agstrcanon_buffer(portname);
	printstring(putstr, f, ":", agstrcanon(portname, buffer2));
	free(buffer2);
    }
}

void write_plain(GVJ_t *job, graph_t *g, void *f, bool extend) {
    char *tport, *hport;
    node_t *n;
    edge_t *e;
    bezier bz;
    pointf pt;
    char *lbl;
    char* fillcolor;

    int (*putstr)(void *chan, const char *str) = g->clos->disc.io->putstr;
    const offsets_t offsets = setYInvert(g);
    pt = GD_bb(g).UR;
    printdouble(putstr, f, "graph ", job->zoom);
    printdouble(putstr, f, " ", PS2INCH(pt.x));
    printdouble(putstr, f, " ", PS2INCH(pt.y));
    agputc(putstr, '\n', f);
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (IS_CLUST_NODE(n))
	    continue;
	char *const node_buffer = agstrcanon_buffer(agnameof(n));
	printstring(putstr, f, "node ", agstrcanon(agnameof(n), node_buffer));
	free(node_buffer);
	printpoint(putstr, f, ND_coord(n), offsets.Y);
	// if HTML, get original text
	char *const value = ND_label(n)->html ? agxget(n, N_label) : ND_label(n)->text;
	char *const buffer = agstrcanon_buffer(value);
	if (ND_label(n)->html) {
	    lbl = agstrcanon(value, buffer);
	} else {
	    lbl = canon(agraphof(n), value, buffer);
	}
        printdouble(putstr, f, " ", ND_width(n));
        printdouble(putstr, f, " ", ND_height(n));
        printstring(putstr, f, " ", lbl);
        free(buffer);
	printstring(putstr, f, " ", late_nnstring(n, N_style, "solid"));
	printstring(putstr, f, " ", ND_shape(n)->name);
	printstring(putstr, f, " ", late_nnstring(n, N_color, DEFAULT_COLOR));
	fillcolor = late_nnstring(n, N_fillcolor, "");
        if (fillcolor[0] == '\0')
	    fillcolor = late_nnstring(n, N_color, DEFAULT_FILL);
	printstring(putstr, f, " ", fillcolor);
	agputc(putstr, '\n', f);
    }
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	for (e = agfstout(g, n); e; e = agnxtout(g, e)) {

	    if (extend) {		//assuming these two attrs have already been created by cgraph
		if (!(tport = agget(e,"tailport")))
		    tport = "";
		if (!(hport = agget(e,"headport")))
		    hport = "";
	    }
	    else
		tport = hport = "";
	    if (ED_spl(e)) {
		size_t splinePoints = 0;
		for (size_t i = 0; i < ED_spl(e)->size; i++) {
		    bz = ED_spl(e)->list[i];
		    splinePoints += bz.size;
		}
		printstring(putstr, f, NULL, "edge");
		writenodeandport(putstr, f, agtail(e), tport);
		writenodeandport(putstr, f, aghead(e), hport);
		printint(putstr, f, " ", splinePoints);
		for (size_t i = 0; i < ED_spl(e)->size; i++) {
		    bz = ED_spl(e)->list[i];
		    for (size_t j = 0; j < bz.size; j++)
			printpoint(putstr, f, bz.list[j], offsets.Y);
		}
	    }
	    if (ED_label(e)) {
		char *const buffer = agstrcanon_buffer(ED_label(e)->text);
		printstring(putstr, f, " ", canon(agraphof(agtail(e)), ED_label(e)->text,
		                                  buffer));
		free(buffer);
		printpoint(putstr, f, ED_label(e)->pos, offsets.Y);
	    }
	    printstring(putstr, f, " ", late_nnstring(e, E_style, "solid"));
	    printstring(putstr, f, " ", late_nnstring(e, E_color, DEFAULT_COLOR));
	    agputc(putstr, '\n', f);
	}
    }
    agputs(putstr, "stop\n", f);
}

static void set_record_rects(node_t *n, field_t *f, agxbuf *xb, double yOff) {
    int i;

    if (f->n_flds == 0) {
	agxbprint(xb, "%.5g,%.5g,%.5g,%.5g ",
		f->b.LL.x + ND_coord(n).x,
		yDir(f->b.LL.y + ND_coord(n).y, yOff),
		f->b.UR.x + ND_coord(n).x,
		yDir(f->b.UR.y + ND_coord(n).y, yOff));
    }
    for (i = 0; i < f->n_flds; i++)
	set_record_rects(n, f->fld[i], xb, yOff);
}

static void rec_attach_bb(graph_t *g, Agsym_t *bbsym, Agsym_t *lpsym,
                          Agsym_t *lwsym, Agsym_t *lhsym, double yOff) {
    int c;
    agxbuf buf = {0};
    pointf pt;

    agxbprint(&buf, "%.5g,%.5g,%.5g,%.5g", GD_bb(g).LL.x,
              yDir(GD_bb(g).LL.y, yOff), GD_bb(g).UR.x,
              yDir(GD_bb(g).UR.y, yOff));
    agxset(g, bbsym, agxbuse(&buf));
    if (GD_label(g) && GD_label(g)->text[0]) {
	pt = GD_label(g)->pos;
	agxbprint(&buf, "%.5g,%.5g", pt.x, yDir(pt.y, yOff));
	agxset(g, lpsym, agxbuse(&buf));
	pt = GD_label(g)->dimen;
	agxbprint(&buf, "%.2f", PS2INCH(pt.x));
	agxset(g, lwsym, agxbuse(&buf));
	agxbprint(&buf, "%.2f", PS2INCH(pt.y));
	agxset(g, lhsym, agxbuse(&buf));
    }
    for (c = 1; c <= GD_n_cluster(g); c++)
	rec_attach_bb(GD_clust(g)[c], bbsym, lpsym, lwsym, lhsym, yOff);
    agxbfree(&buf);
}

double attach_attrs_and_arrows(graph_t *g, bool *sp, bool *ep) {
    node_t *n;
    edge_t *e;
    pointf ptf;
    int dim3 = (GD_odim(g) >= 3);
    Agsym_t* bbsym = NULL;
    Agsym_t* lpsym = NULL;
    Agsym_t* lwsym = NULL;
    Agsym_t* lhsym = NULL;

    gv_fixLocale (1);
    bool e_arrows = false; // graph has edges with end arrows
    bool s_arrows = false; // graph has edges with start arrows
    const offsets_t offsets = setYInvert(g);
    agxbuf xb = {0};
    safe_dcl(g, AGNODE, "pos", "");
    safe_dcl(g, AGNODE, "rects", "");
    N_width = safe_dcl(g, AGNODE, "width", "");
    N_height = safe_dcl(g, AGNODE, "height", "");
    safe_dcl(g, AGEDGE, "pos", "");
    if (GD_has_labels(g) & NODE_XLABEL)
	safe_dcl(g, AGNODE, "xlp", "");
    if (GD_has_labels(g) & EDGE_LABEL)
	safe_dcl(g, AGEDGE, "lp", "");
    if (GD_has_labels(g) & EDGE_XLABEL)
	safe_dcl(g, AGEDGE, "xlp", "");
    if (GD_has_labels(g) & HEAD_LABEL)
	safe_dcl(g, AGEDGE, "head_lp", "");
    if (GD_has_labels(g) & TAIL_LABEL)
	safe_dcl(g, AGEDGE, "tail_lp", "");
    if (GD_has_labels(g) & GRAPH_LABEL) {
	lpsym = safe_dcl(g, AGRAPH, "lp", "");
	lwsym = safe_dcl(g, AGRAPH, "lwidth", "");
	lhsym = safe_dcl(g, AGRAPH, "lheight", "");
    }
    bbsym = safe_dcl(g, AGRAPH, "bb", "");
    for (n = agfstnode(g); n; n = agnxtnode(g, n)) {
	if (dim3) {
	    int k;

	    agxbprint(&xb, "%.5g,%.5g,%.5g", ND_coord(n).x,
	              yDir(ND_coord(n).y, offsets.Y),
	              POINTS_PER_INCH * ND_pos(n)[2]);
	    for (k = 3; k < GD_odim(g); k++) {
		agxbprint(&xb, ",%.5g", POINTS_PER_INCH*(ND_pos(n)[k]));
	    }
	    agset(n, "pos", agxbuse(&xb));
	} else {
	    agxbprint(&xb, "%.5g,%.5g", ND_coord(n).x, yDir(ND_coord(n).y, offsets.Y));
	    agset(n, "pos", agxbuse(&xb));
	}
	agxbprint(&xb, "%.5g", PS2INCH(ND_ht(n)));
	agxset(n, N_height, agxbuse(&xb));
	agxbprint(&xb, "%.5g", PS2INCH(ND_lw(n) + ND_rw(n)));
	agxset(n, N_width, agxbuse(&xb));
	if (ND_xlabel(n) && ND_xlabel(n)->set) {
	    ptf = ND_xlabel(n)->pos;
	    agxbprint(&xb, "%.5g,%.5g", ptf.x, yDir(ptf.y, offsets.Y));
	    agset(n, "xlp", agxbuse(&xb));
	}
	if (strcmp(ND_shape(n)->name, "record") == 0) {
	    set_record_rects(n, ND_shape_info(n), &xb, offsets.Y);
	    agxbpop(&xb);	/* get rid of last space */
	    agset(n, "rects", agxbuse(&xb));
	} else {
	    polygon_t *poly;
	    if (N_vertices && isPolygon(n)) {
		poly = ND_shape_info(n);
		size_t sides = poly->sides;
		if (sides < 3) {
		    char *p = agget(n, "samplepoints");
		    if (p)
			sides = strtoul(p, NULL, 0);
		    else
			sides = 8;
		    if (sides < 3)
			sides = 8;
		}
		for (size_t i = 0; i < sides; i++) {
		    if (i > 0)
			agxbputc(&xb, ' ');
		    if (poly->sides >= 3)
			agxbprint(&xb, "%.5g %.5g",
				PS2INCH(poly->vertices[i].x),
				YFDIR(offsets, PS2INCH(poly->vertices[i].y)));
		    else
			agxbprint(&xb, "%.5g %.5g",
				ND_width(n) / 2.0 * cos((double)i / (double)sides * M_PI * 2.0),
				YFDIR(offsets,
				      ND_height(n) / 2.0 * sin((double)i / (double)sides * M_PI * 2.0)));
		}
		agxset(n, N_vertices, agxbuse(&xb));
	    }
	}
	if (State >= GVSPLINES) {
	    for (e = agfstout(g, n); e; e = agnxtout(g, e)) {
		if (ED_edge_type(e) == IGNORED)
		    continue;
		if (ED_spl(e) == NULL)
		    continue;	/* reported in postproc */
		for (size_t i = 0; i < ED_spl(e)->size; i++) {
		    if (i > 0)
			agxbputc(&xb, ';');
		    if (ED_spl(e)->list[i].sflag) {
			s_arrows = true;
			agxbprint(&xb, "s,%.5g,%.5g ",
				ED_spl(e)->list[i].sp.x,
				yDir(ED_spl(e)->list[i].sp.y, offsets.Y));
		    }
		    if (ED_spl(e)->list[i].eflag) {
			e_arrows = true;
			agxbprint(&xb, "e,%.5g,%.5g ",
				ED_spl(e)->list[i].ep.x,
				yDir(ED_spl(e)->list[i].ep.y, offsets.Y));
		    }
		    for (size_t j = 0; j < ED_spl(e)->list[i].size; j++) {
			if (j > 0)
			    agxbputc(&xb, ' ');
			ptf = ED_spl(e)->list[i].list[j];
			agxbprint(&xb, "%.5g,%.5g", ptf.x, yDir(ptf.y, offsets.Y));
		    }
		}
		agset(e, "pos", agxbuse(&xb));
		if (ED_label(e)) {
		    ptf = ED_label(e)->pos;
		    agxbprint(&xb, "%.5g,%.5g", ptf.x, yDir(ptf.y, offsets.Y));
		    agset(e, "lp", agxbuse(&xb));
		}
		if (ED_xlabel(e) && ED_xlabel(e)->set) {
		    ptf = ED_xlabel(e)->pos;
		    agxbprint(&xb, "%.5g,%.5g", ptf.x, yDir(ptf.y, offsets.Y));
		    agset(e, "xlp", agxbuse(&xb));
		}
		if (ED_head_label(e)) {
		    ptf = ED_head_label(e)->pos;
		    agxbprint(&xb, "%.5g,%.5g", ptf.x, yDir(ptf.y, offsets.Y));
		    agset(e, "head_lp", agxbuse(&xb));
		}
		if (ED_tail_label(e)) {
		    ptf = ED_tail_label(e)->pos;
		    agxbprint(&xb, "%.5g,%.5g", ptf.x, yDir(ptf.y, offsets.Y));
		    agset(e, "tail_lp", agxbuse(&xb));
		}
	    }
	}
    }
    rec_attach_bb(g, bbsym, lpsym, lwsym, lhsym, offsets.Y);
    agxbfree(&xb);

    if (HAS_CLUST_EDGE(g))
	undoClusterEdges(g);
    
    if (sp != NULL) {
	*sp = s_arrows;
    }
    if (ep != NULL) {
	*ep = e_arrows;
    }
    gv_fixLocale (0);
    return offsets.Y;
}

void attach_attrs(graph_t * g)
{
  (void)attach_attrs_and_arrows(g, NULL, NULL);
}

