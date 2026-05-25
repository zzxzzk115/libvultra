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
 *  This library forms the socket for run-time loadable device plugins.  
 */

#include "config.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <util/gv_fopen.h>
#include <util/prisize_t.h>
#include <util/xml.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef HAVE_LIBZ
#include <zlib.h>

#ifndef OS_CODE
#  define OS_CODE  0x03  /* assume Unix */
#endif
static const unsigned char z_file_header[] =
   {0x1f, 0x8b, /*magic*/ Z_DEFLATED, 0 /*flags*/, 0,0,0,0 /*time*/, 0 /*xflags*/, OS_CODE};

static z_stream z_strm;
static unsigned char *df;
static unsigned int dfallocated;
static uint64_t crc;
#endif /* HAVE_LIBZ */

#include <assert.h>
#include <common/const.h>
#include <gvc/gvplugin_device.h>
#include <gvc/gvcjob.h>
#include <gvc/gvcint.h>
#include <gvc/gvcproc.h>
#include <common/utils.h>
#include <gvc/gvio.h>
#include <util/agxbuf.h>
#include <util/exit.h>
#include <util/startswith.h>

static size_t gvwrite_no_z(GVJ_t * job, const void *s, size_t len) {
    if (job->gvc->write_fn)   /* externally provided write discipline */
	return job->gvc->write_fn(job, s, len);
    if (job->output_data) {
	if (len > job->output_data_allocated - (job->output_data_position + 1)) {
	    /* ensure enough allocation for string = null terminator */
	    job->output_data_allocated = job->output_data_position + len + 1;
	    job->output_data = realloc(job->output_data, job->output_data_allocated);
	    if (!job->output_data) {
                job->common->errorfn("memory allocation failure\n");
		graphviz_exit(1);
	    }
	}
	memcpy(job->output_data + job->output_data_position, s, len);
        job->output_data_position += len;
	job->output_data[job->output_data_position] = '\0'; /* keep null terminated */
	return len;
    }
    assert(job->output_file != NULL);
    return fwrite(s, sizeof(char), len, job->output_file);
}

static void auto_output_filename(GVJ_t *job)
{
    static agxbuf buf;
    char *fn;

    if (!(fn = job->input_filename))
        fn = "noname.gv";
    agxbput(&buf, fn);
    if (job->graph_index)
        agxbprint(&buf, ".%d", job->graph_index + 1);
    agxbputc(&buf, '.');

    {
        const char *src = job->output_langname;
        const char *src_end = src + strlen(src);
        for (const char *q = src_end; ; --q) {
            if (*q == ':') {
                agxbprint(&buf, "%.*s.", (int)(src_end - q - 1), q + 1);
                src_end = q;
            }
            if (q == src) {
                agxbprint(&buf, "%.*s", (int)(src_end - src), src);
                break;
            }
        }
    }

    job->output_filename = agxbuse(&buf);
}

/* gvdevice_initialize:
 * Return 0 on success, non-zero on failure
 */
int gvdevice_initialize(GVJ_t * job)
{
    gvdevice_engine_t *gvde = job->device.engine;
    GVC_t *gvc = job->gvc;

    if (gvde && gvde->initialize) {
	gvde->initialize(job);
    }
    else if (job->output_data) {
    }
    /* if the device has no initialization then it uses file output */
    else if (!job->output_file) {        /* if not yet opened */
        if (gvc->common.auto_outfile_names)
            auto_output_filename(job);
        if (job->output_filename) {
            job->output_file = gv_fopen(job->output_filename, "w");
            if (job->output_file == NULL) {
		job->common->errorfn("Could not open \"%s\" for writing : %s\n",
		    job->output_filename, strerror(errno));
                /* perror(job->output_filename); */
                return 1;
            }
        }
        else
            job->output_file = stdout;

#ifdef HAVE_SETMODE
#ifdef O_BINARY
        if (job->flags & GVDEVICE_BINARY_FORMAT)
#ifdef _WIN32
		_setmode(fileno(job->output_file), O_BINARY);
#else
		setmode(fileno(job->output_file), O_BINARY);
#endif			
#endif
#endif
    }

    if (job->flags & GVDEVICE_COMPRESSED_FORMAT) {
#ifdef HAVE_LIBZ
	z_stream *z = &z_strm;

	z->zalloc = 0;
	z->zfree = 0;
	z->opaque = 0;
	z->next_in = NULL;
	z->next_out = NULL;
	z->avail_in = 0;

	crc = crc32(0L, Z_NULL, 0);

	if (deflateInit2(z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
	    job->common->errorfn("Error initializing for deflation\n");
	    return 1;
	}
	gvwrite_no_z(job, z_file_header, sizeof(z_file_header));
#else
	job->common->errorfn("No libz support.\n");
	return 1;
#endif
    }
    return 0;
}

size_t gvwrite (GVJ_t * job, const char *s, size_t len)
{
    size_t ret, olen;

    if (!len || !s)
	return 0;

    if (job->flags & GVDEVICE_COMPRESSED_FORMAT) {
#ifdef HAVE_LIBZ
	z_streamp z = &z_strm;

	size_t dflen = deflateBound(z, len);
	if (dfallocated < dflen) {
	    dfallocated = dflen > UINT_MAX - 1 ? UINT_MAX : (unsigned)dflen + 1;
	    df = realloc(df, dfallocated);
	    if (! df) {
                job->common->errorfn("memory allocation failure\n");
		graphviz_exit(1);
	    }
	}

#if ZLIB_VERNUM >= 0x1290
	crc = crc32_z(crc, (const unsigned char*)s, len);
#else
	crc = crc32(crc, (const unsigned char*)s, len);
#endif

	for (size_t offset = 0; offset < len; ) {
// Suppress Clang/GCC -Wcast-qual warnings. `next_in` is morally const.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	    z->next_in = (unsigned char *)s + offset;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
	    const unsigned chunk = len - offset > UINT_MAX
	                         ? UINT_MAX : (unsigned)(len - offset);
	    z->avail_in = chunk;
	    z->next_out = df;
	    z->avail_out = dfallocated;
	    int r = deflate(z, Z_NO_FLUSH);
	    if (r != Z_OK) {
                job->common->errorfn("deflation problem %d\n", r);
	        graphviz_exit(1);
	    }

	    if ((olen = (size_t)(z->next_out - df))) {
		ret = gvwrite_no_z(job, df, olen);
	        if (ret != olen) {
                    job->common->errorfn("gvwrite_no_z problem %d\n", ret);
	            graphviz_exit(1);
	        }
	    }
	    offset += chunk - z->avail_in;
	}

#else
        (void)olen;
	job->common->errorfn("No libz support.\n");
	graphviz_exit(1);
#endif
    }
    else { /* uncompressed write */
	ret = gvwrite_no_z (job, s, len);
	if (ret != len) {
	    job->common->errorfn("gvwrite_no_z problem %d\n", len);
	    graphviz_exit(1);
	}
    }
    return len;
}

int gvferror (FILE* stream)
{
    GVJ_t *job = (GVJ_t*)stream;

    if (!job->gvc->write_fn && !job->output_data)
	return ferror(job->output_file);

    return 0;
}

int gvputs(GVJ_t * job, const char *s)
{
    size_t len = strlen(s);

    if (gvwrite (job, s, len) != len) {
	return EOF;
    }
    return 1;
}

/// wrap `gvputs` to offer a `void *` first parameter
static int gvputs_wrapper(void *state, const char *s) {
  return gvputs(state, s);
}

int gvputs_xml(GVJ_t *job, const char *s) {
  const xml_flags_t flags = {.dash = 1, .nbsp = 1};
  return gv_xml_escape(s, flags, gvputs_wrapper, job);
}

void gvputs_nonascii(GVJ_t *job, const char *s) {
  for (; *s != '\0'; ++s) {
    if (*s == '\\') {
      gvputs(job, "\\\\");
    } else if (isascii((int)*s)) {
      gvputc(job, *s);
    } else {
      gvprintf(job, "%03o", (unsigned)*s);
    }
  }
}

int gvputc(GVJ_t * job, int c)
{
    const char cc = (char)c;

    if (gvwrite (job, &cc, 1) != 1) {
	return EOF;
    }
    return c;
}

int gvflush (GVJ_t * job)
{
    if (job->output_file
      && ! job->external_context
      && ! job->gvc->write_fn) {
	return fflush(job->output_file);
    }
    else
	return 0;
}

static void gvdevice_close(GVJ_t * job)
{
    if (job->output_filename
      && job->output_file != stdout 
      && ! job->external_context) {
        if (job->output_file) {
            fclose(job->output_file);
            job->output_file = NULL;
        }
	job->output_filename = NULL;
    }
}

void gvdevice_format(GVJ_t * job)
{
    gvdevice_engine_t *gvde = job->device.engine;

    if (gvde && gvde->format)
	gvde->format(job);
    gvflush (job);
}

void gvdevice_finalize(GVJ_t * job)
{
    gvdevice_engine_t *gvde = job->device.engine;
    bool finalized_p = false;

    if (job->flags & GVDEVICE_COMPRESSED_FORMAT) {
#ifdef HAVE_LIBZ
	z_streamp z = &z_strm;
	unsigned char out[8] = "";
	int ret;
	int cnt = 0;

	z->next_in = out;
	z->avail_in = 0;
	z->next_out = df;
	z->avail_out = dfallocated;
	while ((ret = deflate (z, Z_FINISH)) == Z_OK && (cnt++ <= 100)) {
	    gvwrite_no_z(job, df, (size_t)(z->next_out - df));
	    z->next_out = df;
	    z->avail_out = dfallocated;
	}
	if (ret != Z_STREAM_END) {
            job->common->errorfn("deflation finish problem %d cnt=%d\n", ret, cnt);
	    graphviz_exit(1);
	}
	gvwrite_no_z(job, df, (size_t)(z->next_out - df));

	ret = deflateEnd(z);
	if (ret != Z_OK) {
	    job->common->errorfn("deflation end problem %d\n", ret);
	    graphviz_exit(1);
	}
	out[0] = (unsigned char)crc;
	out[1] = (unsigned char)(crc >> 8);
	out[2] = (unsigned char)(crc >> 16);
	out[3] = (unsigned char)(crc >> 24);
	out[4] = (unsigned char)z->total_in;
	out[5] = (unsigned char)(z->total_in >> 8);
	out[6] = (unsigned char)(z->total_in >> 16);
	out[7] = (unsigned char)(z->total_in >> 24);
	gvwrite_no_z(job, out, sizeof(out));
#else
	job->common->errorfn("No libz support\n");
	graphviz_exit(1);
#endif
    }

    if (gvde) {
	if (gvde->finalize) {
	    gvde->finalize(job);
	    finalized_p = true;
	}
    }

    if (! finalized_p) {
        /* if the device has no finalization then it uses file output */
	gvflush (job);
	gvdevice_close(job);
    }
}

void gvprintf(GVJ_t * job, const char *format, ...)
{
    agxbuf buf = {0};
    va_list argp;

    va_start(argp, format);
    int len = vagxbprint(&buf, format, argp);
    if (len < 0) {
	va_end(argp);
	agerrorf("gvprintf: %s\n", strerror(errno));
	return;
    }
    va_end(argp);

    gvwrite(job, agxbuse(&buf), (size_t)len);
    agxbfree(&buf);
}


/* Test with:
 *	cc -DGVPRINTNUM_TEST gvprintnum.c -o gvprintnum
 */

/* use macro so maxnegnum is stated just once for both double and string versions */
#define val_str(n, x) static double n = x; static char n##str[] = #x;
val_str(maxnegnum, -999999999999999.99)

static void gvprintnum(agxbuf *xb, double number) {
    /*
        number limited to a working range: maxnegnum >= n >= -maxnegnum
	suppressing trailing "0" and "."
     */

    if (number < maxnegnum) {		/* -ve limit */
	agxbput(xb, maxnegnumstr);
	return;
    }
    if (number > -maxnegnum) {		/* +ve limit */
	agxbput(xb, maxnegnumstr + 1); // +1 to skip the '-' sign
	return;
    }

    agxbprint(xb, "%.03f", number);
    agxbuf_trim_zeros(xb);

    // strip off unnecessary leading '0'
    {
        char *staging = agxbdisown(xb);
        if (startswith(staging, "0.")) {
            memmove(staging, &staging[1], strlen(staging));
        } else if (startswith(staging, "-0.")) {
            memmove(&staging[1], &staging[2], strlen(&staging[1]));
        }
        agxbput(xb, staging);
        free(staging);
    }
}


#ifdef GVPRINTNUM_TEST
int main (int argc, char *argv[])
{
    agxbuf xb = {0};

    double test[] = {
	-maxnegnum*1.1, -maxnegnum*.9,
	1e8, 10.008, 10, 1, .1, .01,
	.006, .005, .004, .001, 1e-8, 
	0, -0,
	-1e-8, -.001, -.004, -.005, -.006,
	-.01, -.1, -1, -10, -10.008, -1e8,
	maxnegnum*.9, maxnegnum*1.1
    };
    int i = sizeof(test) / sizeof(test[0]);

    while (i--) {
	gvprintnum(&xb, test[i]);
	const char *buf = agxbuse(&xb);
	const size_t len = strlen(buf);
        printf("%g = %s %" PRISIZE_T "\n", test[i], buf, len);
    }

    agxbfree(&xb);
    graphviz_exit(0);
}
#endif

/* gv_trim_zeros
* Identify Trailing zeros and decimal point, if possible.
* Assumes the input is the result of %.02f printing.
*/
static size_t gv_trim_zeros(const char *buf) {
  char *dotp = strchr(buf, '.');
  if (dotp == NULL) {
    return strlen(buf);
  }

  // check this really is the result of %.02f printing
  assert(isdigit((int)dotp[1]) && isdigit((int)dotp[2]) && dotp[3] == '\0');

  if (dotp[2] == '0') {
    if (dotp[1] == '0') {
      return (size_t)(dotp - buf);
    } else {
      return (size_t)(dotp - buf) + 2;
    }
  }

  return strlen(buf);
}

void gvprintdouble(GVJ_t * job, double num)
{
    // Prevents values like -0
    if (num > -0.005 && num < 0.005)
    {
        gvwrite(job, "0", 1);
        return;
    }

    char buf[50];

    snprintf(buf, 50, "%.02f", num);
    size_t len = gv_trim_zeros(buf);

    gvwrite(job, buf, len);
}

void gvprintpointf(GVJ_t * job, pointf p)
{
    agxbuf xb = {0};

    gvprintnum(&xb, p.x);
    const char *buf = agxbuse(&xb);
    gvwrite(job, buf, strlen(buf));
    gvwrite(job, " ", 1);
    gvprintnum(&xb, p.y);
    buf = agxbuse(&xb);
    gvwrite(job, buf, strlen(buf));
    agxbfree(&xb);
} 

void gvprintpointflist(GVJ_t *job, pointf *p, size_t n) {
  const char *separator = "";
  for (size_t i = 0; i < n; ++i) {
    gvputs(job, separator);
    gvprintpointf(job, p[i]);
    separator = " ";
  }
} 
