/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_HTML_HTMLPARSE_H_INCLUDED
# define YY_HTML_HTMLPARSE_H_INCLUDED
/* Debug traces.  */
#ifndef HTMLDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define HTMLDEBUG 1
#  else
#   define HTMLDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define HTMLDEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined HTMLDEBUG */
#if HTMLDEBUG
extern int htmldebug;
#endif
/* "%code requires" blocks.  */
#line 26 "../../lib/common/htmlparse.y"

#include <common/htmllex.h>
#include <common/htmltable.h>
#include <common/textspan.h>
#include <gvc/gvcext.h>
#include <util/agxbuf.h>
#include <util/list.h>
#include <util/strview.h>

#line 67 "htmlparse.h"

/* Token kinds.  */
#ifndef HTMLTOKENTYPE
# define HTMLTOKENTYPE
  enum htmltokentype
  {
    HTMLEMPTY = -2,
    HTMLEOF = 0,                   /* "end of file"  */
    HTMLerror = 256,               /* error  */
    HTMLUNDEF = 257,               /* "invalid token"  */
    T_end_br = 258,                /* T_end_br  */
    T_end_img = 259,               /* T_end_img  */
    T_row = 260,                   /* T_row  */
    T_end_row = 261,               /* T_end_row  */
    T_html = 262,                  /* T_html  */
    T_end_html = 263,              /* T_end_html  */
    T_end_table = 264,             /* T_end_table  */
    T_end_cell = 265,              /* T_end_cell  */
    T_end_font = 266,              /* T_end_font  */
    T_string = 267,                /* T_string  */
    T_error = 268,                 /* T_error  */
    T_n_italic = 269,              /* T_n_italic  */
    T_n_bold = 270,                /* T_n_bold  */
    T_n_underline = 271,           /* T_n_underline  */
    T_n_overline = 272,            /* T_n_overline  */
    T_n_sup = 273,                 /* T_n_sup  */
    T_n_sub = 274,                 /* T_n_sub  */
    T_n_s = 275,                   /* T_n_s  */
    T_HR = 276,                    /* T_HR  */
    T_hr = 277,                    /* T_hr  */
    T_end_hr = 278,                /* T_end_hr  */
    T_VR = 279,                    /* T_VR  */
    T_vr = 280,                    /* T_vr  */
    T_end_vr = 281,                /* T_end_vr  */
    T_BR = 282,                    /* T_BR  */
    T_br = 283,                    /* T_br  */
    T_IMG = 284,                   /* T_IMG  */
    T_img = 285,                   /* T_img  */
    T_table = 286,                 /* T_table  */
    T_cell = 287,                  /* T_cell  */
    T_font = 288,                  /* T_font  */
    T_italic = 289,                /* T_italic  */
    T_bold = 290,                  /* T_bold  */
    T_underline = 291,             /* T_underline  */
    T_overline = 292,              /* T_overline  */
    T_sup = 293,                   /* T_sup  */
    T_sub = 294,                   /* T_sub  */
    T_s = 295                      /* T_s  */
  };
  typedef enum htmltokentype htmltoken_kind_t;
#endif
/* Token kinds.  */
#define HTMLEMPTY -2
#define HTMLEOF 0
#define HTMLerror 256
#define HTMLUNDEF 257
#define T_end_br 258
#define T_end_img 259
#define T_row 260
#define T_end_row 261
#define T_html 262
#define T_end_html 263
#define T_end_table 264
#define T_end_cell 265
#define T_end_font 266
#define T_string 267
#define T_error 268
#define T_n_italic 269
#define T_n_bold 270
#define T_n_underline 271
#define T_n_overline 272
#define T_n_sup 273
#define T_n_sub 274
#define T_n_s 275
#define T_HR 276
#define T_hr 277
#define T_end_hr 278
#define T_VR 279
#define T_vr 280
#define T_end_vr 281
#define T_BR 282
#define T_br 283
#define T_IMG 284
#define T_img 285
#define T_table 286
#define T_cell 287
#define T_font 288
#define T_italic 289
#define T_bold 290
#define T_underline 291
#define T_overline 292
#define T_sup 293
#define T_sub 294
#define T_s 295

/* Value type.  */
#if ! defined HTMLSTYPE && ! defined HTMLSTYPE_IS_DECLARED
union HTMLSTYPE
{
#line 174 "../../lib/common/htmlparse.y"

  int    i;
  htmltxt_t*  txt;
  htmlcell_t*  cell;
  htmltbl_t*   tbl;
  textfont_t*  font;
  htmlimg_t*   img;
  row_t *p;

#line 177 "htmlparse.h"

};
typedef union HTMLSTYPE HTMLSTYPE;
# define HTMLSTYPE_IS_TRIVIAL 1
# define HTMLSTYPE_IS_DECLARED 1
#endif




int htmlparse (htmlscan_t *scanner);

/* "%code provides" blocks.  */
#line 36 "../../lib/common/htmlparse.y"


static inline void free_ti(textspan_t item) {
  free(item.str);
}

static inline void free_hi(htextspan_t item) {
  for (size_t i = 0; i < item.nitems; i++) {
    free(item.items[i].str);
  }
  free(item.items);
}

struct htmlparserstate_s {
  htmllabel_t* lbl;       /* Generated label */
  htmltbl_t*   tblstack;  /* Stack of tables maintained during parsing */
  LIST(textspan_t)  fitemList;
  LIST(htextspan_t) fspanList;
  agxbuf*      str;       /* Buffer for text */
  LIST(textfont_t *)      fontstack;
  GVC_t*       gvc;
};

typedef struct {
#ifdef HAVE_EXPAT
    struct XML_ParserStruct *parser;
#endif
    char* ptr;         // input source
    int tok;           // token type
    agxbuf* xb;        // buffer to gather T_string data
    agxbuf lb;         // buffer for translating lexical data
    int warn;          // set if warning given
    int error;         // set if error given
    char inCell;       // set if in TD to allow T_string
    char mode;         // for handling artificial <HTML>..</HTML>
    strview_t currtok; // for error reporting
    strview_t prevtok; // for error reporting
    GVC_t *gvc;        // current GraphViz context
    HTMLSTYPE *htmllval; // generated by htmlparse.y
} htmllexstate_t;


struct htmlscan_s {
  htmllexstate_t lexer;
  htmlparserstate_t parser;
};

#line 239 "htmlparse.h"

#endif /* !YY_HTML_HTMLPARSE_H_INCLUDED  */
