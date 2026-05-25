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
 *  textlayout engine wrapper
 */

#include "config.h"

#include <common/const.h>
#include <gvc/gvplugin_textlayout.h>
#include <gvc/gvcint.h>
#include <gvc/gvcproc.h>
#include <stdbool.h>
#include <stdio.h>

int gvtextlayout_select(GVC_t * gvc)
{
    gvplugin_available_t *plugin;
    gvplugin_installed_t *typeptr;

    plugin = gvplugin_load(gvc, API_textlayout, "textlayout", NULL);
    if (plugin) {
	typeptr = plugin->typeptr;
	gvc->textlayout.engine = typeptr->engine;
	return GVRENDER_PLUGIN;  /* FIXME - need more suitable success code */
    }
    return NO_SUPPORT;
}

bool gvtextlayout(GVC_t *gvc, textspan_t *span, char **fontpath)
{
    gvtextlayout_engine_t *gvte = gvc->textlayout.engine;

    if (gvte && gvte->textlayout)
	return gvte->textlayout(span, fontpath);
    return false;
}
