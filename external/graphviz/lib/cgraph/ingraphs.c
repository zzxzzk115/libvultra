/**
 * @file
 * @brief supports user-supplied data
 * @ingroup cgraph_app
 */
/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/org/documents/epl-2.0/EPL-2.0.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/


/*
 * Written by Emden Gansner
 */

#include "config.h"

#include <cgraph/cgraph.h>
#include <cgraph/ingraphs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <util/gv_fopen.h>

/* Set next available file.
 * If Files is NULL, we just read from stdin.
 */
static void nextFile(ingraph_state * sp)
{
    void *rv = NULL;
    char *fname;

    if (sp->u.Files == NULL) {
	if (sp->ctr++ == 0) {
	    rv = stdin;
	}
    } else {
	while ((fname = sp->u.Files[sp->ctr++])) {
	    if (*fname == '-') {
		rv = stdin;
		break;
	    } else if ((rv = gv_fopen(fname, "r")) != 0)
		break;
	    else {
		fprintf(stderr, "Can't open %s\n", sp->u.Files[sp->ctr - 1]);
		sp->errors++;
	    }
	}
    }
    sp->fp = rv;
}

/* Read and return next graph; return NULL if done.
 * Read graph from currently open file. If none, open next file.
 */
Agraph_t *nextGraph(ingraph_state * sp)
{
    Agraph_t *g;

    if (sp->ingraphs) {
	g = sp->u.Graphs[sp->ctr];
	if (g) sp->ctr++;
	return g;
    }
    if (sp->fp == NULL)
	nextFile(sp);
    g = NULL;

    while (sp->fp != NULL) {
	if ((g = sp->readf(fileName(sp), sp->fp)) != 0)
	    break;
	if (sp->u.Files)	/* Only close if not using stdin */
	    (void)fclose(sp->fp);
	nextFile(sp);
    }
    return g;
}

/* Create new ingraph state. If sp is non-NULL, we
 * assume user is supplying memory.
 */
static ingraph_state *new_ing(ingraph_state *sp, char **files,
                              Agraph_t **graphs,
                              Agraph_t *(*readf)(const char *, void *)) {
    if (!sp) {
	sp = malloc(sizeof(ingraph_state));
	if (!sp) {
	    fprintf(stderr, "ingraphs: out of memory\n");
	    return 0;
	}
	sp->heap = true;
    } else
	sp->heap = false;
    if (graphs) {
	sp->ingraphs = 1;
	sp->u.Graphs = graphs;
    }
    else {
	sp->ingraphs = 0;
	sp->u.Files = files;
    }
    sp->ctr = 0;
    sp->errors = 0;
    sp->fp = NULL;
    if (!readf) {
	if (sp->heap)
	    free(sp);
	fprintf(stderr, "ingraphs: NULL read function\n");
	return 0;
    }
    sp->readf = readf;
    return sp;
}

ingraph_state *newIng(ingraph_state *sp, char **files,
                      Agraph_t *(*readf)(const char *, void *)) {
  return new_ing(sp, files, 0, readf);
}

/* Create new ingraph state using supplied graphs. If sp is non-NULL, we
 * assume user is supplying memory.
 */
ingraph_state *newIngGraphs(ingraph_state *sp, Agraph_t **graphs,
                            Agraph_t *(*readf)(const char *, void *)) {
  return new_ing(sp, 0, graphs, readf);
}

static Agraph_t *dflt_read(const char *filename, void *fp) {
  return agconcat(NULL, filename, fp, NULL);
}

/* At present, we require opf to be non-NULL. In
 * theory, we could assume a function agread(FILE*,void*)
 */
ingraph_state *newIngraph(ingraph_state * sp, char **files) {
  return newIng(sp, files, dflt_read);
}

/* Close any open files and free discipline
 * Free sp if necessary.
 */
void closeIngraph(ingraph_state * sp)
{
    if (!sp->ingraphs && sp->u.Files && sp->fp)
	(void)fclose(sp->fp);
    if (sp->heap)
	free(sp);
}

/// Return name of current file being processed.
char *fileName(ingraph_state * sp)
{
    char *fname;

    if (sp->ingraphs) {
	return "<>";
    }
    else if (sp->u.Files) {
	if (sp->ctr) {
	    fname = sp->u.Files[sp->ctr - 1];
	    if (*fname == '-')
		return "<stdin>";
	    else
		return fname;
	} else
	    return "<>";
    } else
	return "<stdin>";
}
