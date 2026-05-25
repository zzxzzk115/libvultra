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

/* FIXME
 * This is an ugly mess.
 *
 * Args should be made independent of layout engine and arg values
 * should be stored in gvc or gvc->job.   All globals should be eliminated.
 *
 * Needs to be fixed before layout engines can be plugins.
 */

#include "config.h"

#include <assert.h>
#include <common/render.h>
#include <fdpgen/tlayout.h>
#include <gvc/gvc.h>
#include <fdpgen/fdp.h>
#include <stdbool.h>
#include <util/gv_ctype.h>

/* Handle special neato arguments.
 * Return number of unprocessed arguments; return < 0 on error.
 */
static int neato_extra_args(int argc, char** argv) {
  char** p = argv+1;
  int    i;
  char*  arg;
  int    cnt = 1;

  for (i = 1; i < argc; i++) {
    arg = argv[i];
    assert(arg != NULL);
    if (arg[0] == '-') {
      switch (arg[1]) {
      case 'x' : Reduce = true; break;
      case 'n':
        if (arg[2]) {
          Nop = atoi(arg+2);
          if (Nop <= 0) {
            agerrorf("Invalid parameter \"%s\" for -n flag\n", arg+2);
            dotneato_usage(argv[0], 1);
	    return -1;
          }
        }
        else Nop = 1;
        break;
      default :
        cnt++;
        if (*p != arg) *p = arg;
        p++;
        break;
      }
    }
    else {
      cnt++;
      if (*p != arg) *p = arg;
      p++;
    }
  }
  *p = 0;
  return cnt;
}

/* Handle special config arguments.
 * Return number of unprocessed arguments; return < 0 on error.
 */
static int
config_extra_args(GVC_t *gvc, int argc, char** argv)
{
  char** p = argv+1;
  int    i;
  char*  arg;
  int    cnt = 1;

  for (i = 1; i < argc; i++) {
    arg = argv[i];
    assert(arg != NULL);
    if (arg[0] == '-') {
      switch (arg[1]) {
      case 'v':
	gvc->common.verbose = 1;
	if (gv_isdigit(arg[2]))
	  gvc->common.verbose = atoi(&arg[2]);
        break;
      case 'O' :
          gvc->common.auto_outfile_names = true;
	  break;
      case 'c' :
          gvc->common.config = true;
	  break;
      default :
        cnt++;
        if (*p != arg) *p = arg;
        p++;
        break;
      }
    }
    else {
      cnt++;
      if (*p != arg) *p = arg;
      p++;
    }
  }
  *p = 0;
  return cnt;
}

/* If arg is an double, value is stored in v
 * and functions returns 0; otherwise, returns 1.
 */
static int
setDouble (double* v, char* arg)
{
  char*    p;
  double   d;

  d = strtod(arg,&p);
  if (p == arg) {
    agerrorf("bad value in flag -L%s - ignored\n", arg-1);
    return 1;
  }
  *v = d;
  return 0;
}

/* If arg is an integer, value is stored in v
 * and functions returns 0; otherwise, returns 1.
 */
static int
setInt (int* v, char* arg)
{
  char*    p;
  int      i;

  i = (int)strtol(arg,&p,10);
  if (p == arg) {
    agerrorf("bad value in flag -L%s - ignored\n", arg-1);
    return 1;
  }
  *v = i;
  return 0;
}

/// Actions for fdp specific flags
static int
setFDPAttr (char* arg)
{
  switch (*arg++) {
  case 'g' :
    fdp_parms->useGrid = 0;
    break;
  case 'O' :
    fdp_parms->useNew = 0;
    break;
  case 'n' :
    if (setInt (&fdp_parms->numIters, arg)) return 1;
    break;
  case 'U' :
    if (setInt (&fdp_parms->unscaled, arg)) return 1;
    break;
  case 'C' :
    if (setDouble (&fdp_parms->C, arg)) return 1;
    break;
  case 'T' :
    if (*arg == '*') {
      if (setDouble (&fdp_parms->Tfact, arg+1)) return 1;
    }
    else {
      if (setDouble (&fdp_parms->T0, arg)) return 1;
    }
    break;
  default :
    agwarningf("unknown flag -L%s - ignored\n", arg-1);
    break;
  }
  return 0;
}

/* Handle fdp specific arguments.
 * These have the form -L<name>=<value>.
 * Return number of unprocessed arguments; return < 0 on error.
 */
static int fdp_extra_args(int argc, char** argv) {
  char** p = argv+1;
  int    i;
  char*  arg;
  int    cnt = 1;

  for (i = 1; i < argc; i++) {
    arg = argv[i];
    assert(arg != NULL);
    if (arg[0] == '-' && arg[1] == 'L') {
      if (setFDPAttr (arg+2)) {
	dotneato_usage(argv[0], 1);
	return -1;
      }
    }
    else {
      cnt++;
      if (*p != arg) *p = arg;
      p++;
    }
  }
  *p = 0;
  return cnt;
}

/* Return 0 on success.
 * Return x if calling function should call exit(x-1).
 */
int gvParseArgs(GVC_t *gvc, int argc, char** argv)
{
    int rv;
    if ((argc = neato_extra_args(argc, argv)) < 0)
	return (1-argc);
    if ((argc = fdp_extra_args(argc, argv)) < 0)
	return (1-argc);
    if ((argc = config_extra_args(gvc, argc, argv)) < 0)
	return (1-argc);
    if ((rv = dotneato_args_initialize(gvc, argc, argv)))
	return rv;
    if (Verbose)
	gvplugin_write_status(gvc);
    return 0;
}
