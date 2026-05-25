/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/org/documents/epl-2.0/EPL-2.0.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

#include <common/render.h>
#include <cdt/cdt.h>

    typedef struct _grid Grid;

    typedef struct _node_list {
	Agnode_t *node;
	struct _node_list *next;
    } node_list;

    typedef struct {
	int i, j;
    } gridpt;

    typedef struct {
	gridpt p;		/* index of cell */
	node_list *nodes;	/* nodes in cell */
	Dtlink_t link;		/* cdt data */
    } cell;

    extern Grid *mkGrid(int);
    extern void adjustGrid(Grid * g, int nnodes);
    extern void clearGrid(Grid *);
    extern void addGrid(Grid *, int, int, Agnode_t *);
    extern void walkGrid(Grid *, int (*)(void *, void *));
    extern cell *findGrid(Grid *, int, int);
    extern void delGrid(Grid *);
    extern int gLength(cell * p);

#ifdef __cplusplus
}
#endif
