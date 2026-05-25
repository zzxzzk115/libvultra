/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Substitute the type names.  */
#define YYSTYPE         HTMLSTYPE
/* Substitute the variable and function names.  */
#define yyparse         htmlparse
#define yylex           htmllex
#define yyerror         htmlerror
#define yydebug         htmldebug
#define yynerrs         htmlnerrs

/* First part of user prologue.  */
#line 84 "../../lib/common/htmlparse.y"


#include <common/render.h>
#include <common/htmltable.h>
#include <common/htmllex.h>
#include <stdbool.h>
#include <util/alloc.h>

/// Clean up cell if error in parsing.
static void cleanCell(htmlcell_t *cp);

/// Clean up table if error in parsing.
static void cleanTbl(htmltbl_t *tp) {
  rows_t *rows = &tp->rows;
  for (size_t r = 0; r < LIST_SIZE(rows); ++r) {
    row_t *rp = LIST_GET(rows, r);
    for (size_t c = 0; c < LIST_SIZE(&rp->rp); ++c) {
      cleanCell(LIST_GET(&rp->rp, c));
    }
  }
  LIST_FREE(rows);
  free_html_data(&tp->data);
  free(tp);
}

/// Clean up cell if error in parsing.
static void
cleanCell (htmlcell_t* cp)
{
  if (cp->child.kind == HTML_TBL) cleanTbl (cp->child.u.tbl);
  else if (cp->child.kind == HTML_TEXT) free_html_text (cp->child.u.txt);
  free_html_data (&cp->data);
  free (cp);
}

/// Append a new text span to the list.
static void
appendFItemList (htmlparserstate_t *html_state, agxbuf *ag);

static void
appendFLineList (htmlparserstate_t *html_state, int v);

static htmltxt_t*
mkText(htmlparserstate_t *html_state);

static row_t *lastRow(htmlparserstate_t *html_state);

/// Add new cell row to current table.
static void addRow(htmlparserstate_t *html_state);

/// Set cell body and type and attach to row
static void setCell(htmlparserstate_t *html_state, htmlcell_t *cp, void *obj, label_type_t kind);

/// Create label, given body and type.
static htmllabel_t *mkLabel(void *obj, label_type_t kind) {
  htmllabel_t* lp = gv_alloc(sizeof(htmllabel_t));

  lp->kind = kind;
  if (kind == HTML_TEXT)
    lp->u.txt = obj;
  else
    lp->u.tbl = obj;
  return lp;
}

/* Called on error. Frees resources allocated during parsing.
 * This includes a label, plus a walk down the stack of
 * tables. Note that `cleanTbl` frees the contained cells.
 */
static void cleanup (htmlparserstate_t *html_state);

/// Return true if s contains a non-space character.
static bool nonSpace(const char *s) {
  char   c;

  while ((c = *s++)) {
    if (c != ' ') return true;
  }
  return false;
}

/// Fonts are allocated in the lexer.
static void
pushFont (htmlparserstate_t *html_state, textfont_t *fp);

static void
popFont (htmlparserstate_t *html_state);


#line 167 "htmlparse.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "htmlparse.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_T_end_br = 3,                   /* T_end_br  */
  YYSYMBOL_T_end_img = 4,                  /* T_end_img  */
  YYSYMBOL_T_row = 5,                      /* T_row  */
  YYSYMBOL_T_end_row = 6,                  /* T_end_row  */
  YYSYMBOL_T_html = 7,                     /* T_html  */
  YYSYMBOL_T_end_html = 8,                 /* T_end_html  */
  YYSYMBOL_T_end_table = 9,                /* T_end_table  */
  YYSYMBOL_T_end_cell = 10,                /* T_end_cell  */
  YYSYMBOL_T_end_font = 11,                /* T_end_font  */
  YYSYMBOL_T_string = 12,                  /* T_string  */
  YYSYMBOL_T_error = 13,                   /* T_error  */
  YYSYMBOL_T_n_italic = 14,                /* T_n_italic  */
  YYSYMBOL_T_n_bold = 15,                  /* T_n_bold  */
  YYSYMBOL_T_n_underline = 16,             /* T_n_underline  */
  YYSYMBOL_T_n_overline = 17,              /* T_n_overline  */
  YYSYMBOL_T_n_sup = 18,                   /* T_n_sup  */
  YYSYMBOL_T_n_sub = 19,                   /* T_n_sub  */
  YYSYMBOL_T_n_s = 20,                     /* T_n_s  */
  YYSYMBOL_T_HR = 21,                      /* T_HR  */
  YYSYMBOL_T_hr = 22,                      /* T_hr  */
  YYSYMBOL_T_end_hr = 23,                  /* T_end_hr  */
  YYSYMBOL_T_VR = 24,                      /* T_VR  */
  YYSYMBOL_T_vr = 25,                      /* T_vr  */
  YYSYMBOL_T_end_vr = 26,                  /* T_end_vr  */
  YYSYMBOL_T_BR = 27,                      /* T_BR  */
  YYSYMBOL_T_br = 28,                      /* T_br  */
  YYSYMBOL_T_IMG = 29,                     /* T_IMG  */
  YYSYMBOL_T_img = 30,                     /* T_img  */
  YYSYMBOL_T_table = 31,                   /* T_table  */
  YYSYMBOL_T_cell = 32,                    /* T_cell  */
  YYSYMBOL_T_font = 33,                    /* T_font  */
  YYSYMBOL_T_italic = 34,                  /* T_italic  */
  YYSYMBOL_T_bold = 35,                    /* T_bold  */
  YYSYMBOL_T_underline = 36,               /* T_underline  */
  YYSYMBOL_T_overline = 37,                /* T_overline  */
  YYSYMBOL_T_sup = 38,                     /* T_sup  */
  YYSYMBOL_T_sub = 39,                     /* T_sub  */
  YYSYMBOL_T_s = 40,                       /* T_s  */
  YYSYMBOL_YYACCEPT = 41,                  /* $accept  */
  YYSYMBOL_html = 42,                      /* html  */
  YYSYMBOL_fonttext = 43,                  /* fonttext  */
  YYSYMBOL_text = 44,                      /* text  */
  YYSYMBOL_textitem = 45,                  /* textitem  */
  YYSYMBOL_font = 46,                      /* font  */
  YYSYMBOL_n_font = 47,                    /* n_font  */
  YYSYMBOL_italic = 48,                    /* italic  */
  YYSYMBOL_n_italic = 49,                  /* n_italic  */
  YYSYMBOL_bold = 50,                      /* bold  */
  YYSYMBOL_n_bold = 51,                    /* n_bold  */
  YYSYMBOL_strike = 52,                    /* strike  */
  YYSYMBOL_n_strike = 53,                  /* n_strike  */
  YYSYMBOL_underline = 54,                 /* underline  */
  YYSYMBOL_n_underline = 55,               /* n_underline  */
  YYSYMBOL_overline = 56,                  /* overline  */
  YYSYMBOL_n_overline = 57,                /* n_overline  */
  YYSYMBOL_sup = 58,                       /* sup  */
  YYSYMBOL_n_sup = 59,                     /* n_sup  */
  YYSYMBOL_sub = 60,                       /* sub  */
  YYSYMBOL_n_sub = 61,                     /* n_sub  */
  YYSYMBOL_br = 62,                        /* br  */
  YYSYMBOL_string = 63,                    /* string  */
  YYSYMBOL_table = 64,                     /* table  */
  YYSYMBOL_65_1 = 65,                      /* @1  */
  YYSYMBOL_fonttable = 66,                 /* fonttable  */
  YYSYMBOL_opt_space = 67,                 /* opt_space  */
  YYSYMBOL_rows = 68,                      /* rows  */
  YYSYMBOL_row = 69,                       /* row  */
  YYSYMBOL_70_2 = 70,                      /* $@2  */
  YYSYMBOL_cells = 71,                     /* cells  */
  YYSYMBOL_cell = 72,                      /* cell  */
  YYSYMBOL_73_3 = 73,                      /* $@3  */
  YYSYMBOL_74_4 = 74,                      /* $@4  */
  YYSYMBOL_75_5 = 75,                      /* $@5  */
  YYSYMBOL_76_6 = 76,                      /* $@6  */
  YYSYMBOL_image = 77,                     /* image  */
  YYSYMBOL_HR = 78,                        /* HR  */
  YYSYMBOL_VR = 79                         /* VR  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined HTMLSTYPE_IS_TRIVIAL && HTMLSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  31
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   271

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  41
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  39
/* YYNRULES -- Number of rules.  */
#define YYNRULES  69
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  116

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   295


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40
};

#if HTMLDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   206,   206,   207,   208,   211,   214,   215,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,   230,   233,
     236,   239,   242,   245,   248,   251,   254,   257,   260,   263,
     266,   269,   272,   275,   278,   279,   282,   283,   286,   286,
     307,   308,   309,   310,   311,   312,   315,   316,   319,   320,
     321,   324,   324,   327,   328,   329,   332,   332,   333,   333,
     334,   334,   335,   335,   338,   339,   342,   343,   346,   347
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if HTMLDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "T_end_br",
  "T_end_img", "T_row", "T_end_row", "T_html", "T_end_html", "T_end_table",
  "T_end_cell", "T_end_font", "T_string", "T_error", "T_n_italic",
  "T_n_bold", "T_n_underline", "T_n_overline", "T_n_sup", "T_n_sub",
  "T_n_s", "T_HR", "T_hr", "T_end_hr", "T_VR", "T_vr", "T_end_vr", "T_BR",
  "T_br", "T_IMG", "T_img", "T_table", "T_cell", "T_font", "T_italic",
  "T_bold", "T_underline", "T_overline", "T_sup", "T_sub", "T_s",
  "$accept", "html", "fonttext", "text", "textitem", "font", "n_font",
  "italic", "n_italic", "bold", "n_bold", "strike", "n_strike",
  "underline", "n_underline", "overline", "n_overline", "sup", "n_sup",
  "sub", "n_sub", "br", "string", "table", "@1", "fonttable", "opt_space",
  "rows", "row", "$@2", "cells", "cell", "$@3", "$@4", "$@5", "$@6",
  "image", "HR", "VR", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-82)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-63)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
       8,   -82,   209,    10,   -82,   -82,    11,   -82,   -82,   -82,
     -82,   -82,   -82,   -82,   -82,     5,   209,   -82,   209,   209,
     209,   209,   209,   209,   209,   209,   -82,    -5,   -82,    14,
     -20,   -82,   -82,   -82,   -82,   209,   209,   209,   209,   209,
      13,    37,    12,    66,    16,    80,    19,   109,   123,    20,
     152,    15,   166,   195,   -82,   -82,   -82,   -82,   -82,   -82,
     -82,   -82,   -82,   -82,   -82,   -82,   -82,   -82,   -82,   -82,
     -82,   -82,   -82,   -82,   -82,   -82,   -82,   -82,    23,   -82,
     119,   -82,     7,    46,   -82,    38,   -82,    23,    17,    35,
     -82,    13,   -82,   -82,   -82,   -82,    58,   -82,   -82,    53,
     -82,   -82,   -82,    40,   -82,     7,   -82,    59,    69,   -82,
      72,   -82,   -82,   -82,   -82,   -82
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     4,    47,     0,    36,    35,     0,    18,    20,    22,
      26,    28,    30,    32,    24,     0,     5,     7,    47,    47,
      47,     0,    47,    47,     0,     0,     9,     8,    40,     0,
       0,     1,    34,     2,     6,     0,     0,     0,     0,     0,
       8,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    37,     3,    38,    19,    10,    41,
      21,    11,    42,    23,    14,    45,    25,    17,    27,    12,
      43,    29,    13,    44,    31,    15,    33,    16,     0,    51,
       0,    48,     0,    47,    67,     0,    49,     0,    47,     0,
      53,    46,    39,    66,    50,    65,     0,    58,    56,     0,
      60,    52,    69,     0,    54,     0,    64,     0,     0,    63,
       0,    68,    55,    59,    57,    61
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -82,   -82,    -4,   232,   -10,    -1,    26,     0,    39,     1,
      50,   -82,   -82,     2,    36,     3,    47,   -82,   -82,   -82,
     -82,   -82,    -2,   148,   -82,     9,    27,   -82,   -68,   -82,
     -82,   -81,   -82,   -82,   -82,   -82,   -82,   -82,   -82
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     3,    15,    16,    17,    35,    58,    36,    61,    37,
      64,    21,    67,    38,    69,    39,    72,    24,    75,    25,
      77,    26,    40,    28,    78,    29,    30,    80,    81,    82,
      89,    90,   108,   107,   110,    99,   100,    87,   105
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      27,    18,    19,    20,    22,    23,    34,    54,   104,     1,
      31,    56,    86,    33,    32,     2,    27,    27,    27,    94,
      27,    27,    55,    57,   112,    54,   -46,   -62,    79,     4,
      60,    34,    71,    34,    63,    34,    68,    34,    34,    88,
      34,   101,    34,    34,     5,     6,    95,    96,    57,     4,
       7,     8,     9,    10,    11,    12,    13,    14,     4,   102,
     103,    93,   106,   109,     5,     6,   111,    88,    59,   113,
       7,     8,     9,    10,    11,    12,    13,    14,     4,   114,
      60,    91,   115,    62,    97,    70,    27,    18,    19,    20,
      22,    23,     4,     5,     6,    63,    65,    98,    73,     7,
       8,     9,    10,    11,    12,    13,    14,     5,     6,     0,
      92,     0,     0,     7,     8,     9,    10,    11,    12,    13,
      14,     4,     0,     0,    79,     0,     0,     0,    83,    66,
       0,     0,     0,     0,     0,     4,     5,     6,     0,    68,
      84,    85,     7,     8,     9,    10,    11,    12,    13,    14,
       5,     6,     0,     0,     0,     0,     7,     8,     9,    10,
      11,    12,    13,    14,     4,     0,    42,    44,    46,    71,
      49,    51,     0,     0,     0,     0,     0,     0,     4,     5,
       6,     0,     0,     0,    74,     7,     8,     9,    10,    11,
      12,    13,    14,     5,     6,     0,     0,     0,     0,     7,
       8,     9,    10,    11,    12,    13,    14,     4,     0,     0,
       0,     0,     0,     0,    76,     0,     0,     0,     0,     0,
       0,     4,     5,     6,     0,     0,     0,     0,     7,     8,
       9,    10,    11,    12,    13,    14,     5,     6,     0,     0,
       0,     0,     7,     8,     9,    10,    11,    12,    13,    14,
      41,    43,    45,    47,    48,    50,    52,    53,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    41,    43,    45,
      48,    50
};

static const yytype_int8 yycheck[] =
{
       2,     2,     2,     2,     2,     2,    16,    12,    89,     1,
       0,    31,    80,     8,     3,     7,    18,    19,    20,    87,
      22,    23,     8,    11,   105,    12,    31,    10,     5,    12,
      14,    41,    17,    43,    15,    45,    16,    47,    48,    32,
      50,     6,    52,    53,    27,    28,    29,    30,    11,    12,
      33,    34,    35,    36,    37,    38,    39,    40,    12,    24,
      25,    23,     4,    10,    27,    28,    26,    32,    42,    10,
      33,    34,    35,    36,    37,    38,    39,    40,    12,    10,
      14,    83,    10,    44,    88,    49,    88,    88,    88,    88,
      88,    88,    12,    27,    28,    15,    46,    88,    51,    33,
      34,    35,    36,    37,    38,    39,    40,    27,    28,    -1,
      83,    -1,    -1,    33,    34,    35,    36,    37,    38,    39,
      40,    12,    -1,    -1,     5,    -1,    -1,    -1,     9,    20,
      -1,    -1,    -1,    -1,    -1,    12,    27,    28,    -1,    16,
      21,    22,    33,    34,    35,    36,    37,    38,    39,    40,
      27,    28,    -1,    -1,    -1,    -1,    33,    34,    35,    36,
      37,    38,    39,    40,    12,    -1,    18,    19,    20,    17,
      22,    23,    -1,    -1,    -1,    -1,    -1,    -1,    12,    27,
      28,    -1,    -1,    -1,    18,    33,    34,    35,    36,    37,
      38,    39,    40,    27,    28,    -1,    -1,    -1,    -1,    33,
      34,    35,    36,    37,    38,    39,    40,    12,    -1,    -1,
      -1,    -1,    -1,    -1,    19,    -1,    -1,    -1,    -1,    -1,
      -1,    12,    27,    28,    -1,    -1,    -1,    -1,    33,    34,
      35,    36,    37,    38,    39,    40,    27,    28,    -1,    -1,
      -1,    -1,    33,    34,    35,    36,    37,    38,    39,    40,
      18,    19,    20,    21,    22,    23,    24,    25,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    36,    37,
      38,    39
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     1,     7,    42,    12,    27,    28,    33,    34,    35,
      36,    37,    38,    39,    40,    43,    44,    45,    46,    48,
      50,    52,    54,    56,    58,    60,    62,    63,    64,    66,
      67,     0,     3,     8,    45,    46,    48,    50,    54,    56,
      63,    44,    64,    44,    64,    44,    64,    44,    44,    64,
      44,    64,    44,    44,    12,     8,    31,    11,    47,    47,
      14,    49,    49,    15,    51,    51,    20,    53,    16,    55,
      55,    17,    57,    57,    18,    59,    19,    61,    65,     5,
      68,    69,    70,     9,    21,    22,    69,    78,    32,    71,
      72,    63,    67,    23,    69,    29,    30,    43,    66,    76,
      77,     6,    24,    25,    72,    79,     4,    74,    73,    10,
      75,    26,    72,    10,    10,    10
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    41,    42,    42,    42,    43,    44,    44,    45,    45,
      45,    45,    45,    45,    45,    45,    45,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    62,    63,    63,    65,    64,
      66,    66,    66,    66,    66,    66,    67,    67,    68,    68,
      68,    70,    69,    71,    71,    71,    73,    72,    74,    72,
      75,    72,    76,    72,    77,    77,    78,    78,    79,    79
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     3,     3,     1,     1,     2,     1,     1,     1,
       3,     3,     3,     3,     3,     3,     3,     3,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     1,     1,     2,     0,     6,
       1,     3,     3,     3,     3,     3,     1,     0,     1,     2,
       3,     0,     4,     1,     2,     3,     0,     4,     0,     4,
       0,     4,     0,     3,     2,     1,     2,     1,     2,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = HTMLEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == HTMLEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (scanner, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use HTMLerror or HTMLUNDEF. */
#define YYERRCODE HTMLUNDEF


/* Enable debugging if requested.  */
#if HTMLDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, scanner); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, htmlscan_t *scanner)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (scanner);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, htmlscan_t *scanner)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep, scanner);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule, htmlscan_t *scanner)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)], scanner);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule, scanner); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !HTMLDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !HTMLDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, htmlscan_t *scanner)
{
  YY_USE (yyvaluep);
  YY_USE (scanner);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (htmlscan_t *scanner)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = HTMLEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == HTMLEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, scanner);
    }

  if (yychar <= HTMLEOF)
    {
      yychar = HTMLEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == HTMLerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = HTMLUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = HTMLEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* html: T_html fonttext T_end_html  */
#line 206 "../../lib/common/htmlparse.y"
                                   { scanner->parser.lbl = mkLabel((yyvsp[-1].txt),HTML_TEXT); }
#line 1353 "htmlparse.c"
    break;

  case 3: /* html: T_html fonttable T_end_html  */
#line 207 "../../lib/common/htmlparse.y"
                                    { scanner->parser.lbl = mkLabel((yyvsp[-1].tbl),HTML_TBL); }
#line 1359 "htmlparse.c"
    break;

  case 4: /* html: error  */
#line 208 "../../lib/common/htmlparse.y"
              { cleanup(&scanner->parser); YYABORT; }
#line 1365 "htmlparse.c"
    break;

  case 5: /* fonttext: text  */
#line 211 "../../lib/common/htmlparse.y"
                { (yyval.txt) = mkText(&scanner->parser); }
#line 1371 "htmlparse.c"
    break;

  case 8: /* textitem: string  */
#line 218 "../../lib/common/htmlparse.y"
                  { appendFItemList(&scanner->parser,scanner->parser.str);}
#line 1377 "htmlparse.c"
    break;

  case 9: /* textitem: br  */
#line 219 "../../lib/common/htmlparse.y"
              {appendFLineList(&scanner->parser,(yyvsp[0].i));}
#line 1383 "htmlparse.c"
    break;

  case 18: /* font: T_font  */
#line 230 "../../lib/common/htmlparse.y"
              { pushFont (&scanner->parser,(yyvsp[0].font)); }
#line 1389 "htmlparse.c"
    break;

  case 19: /* n_font: T_end_font  */
#line 233 "../../lib/common/htmlparse.y"
                    { popFont (&scanner->parser); }
#line 1395 "htmlparse.c"
    break;

  case 20: /* italic: T_italic  */
#line 236 "../../lib/common/htmlparse.y"
                  {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1401 "htmlparse.c"
    break;

  case 21: /* n_italic: T_n_italic  */
#line 239 "../../lib/common/htmlparse.y"
                      {popFont(&scanner->parser);}
#line 1407 "htmlparse.c"
    break;

  case 22: /* bold: T_bold  */
#line 242 "../../lib/common/htmlparse.y"
              {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1413 "htmlparse.c"
    break;

  case 23: /* n_bold: T_n_bold  */
#line 245 "../../lib/common/htmlparse.y"
                  {popFont(&scanner->parser);}
#line 1419 "htmlparse.c"
    break;

  case 24: /* strike: T_s  */
#line 248 "../../lib/common/htmlparse.y"
             {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1425 "htmlparse.c"
    break;

  case 25: /* n_strike: T_n_s  */
#line 251 "../../lib/common/htmlparse.y"
                 {popFont(&scanner->parser);}
#line 1431 "htmlparse.c"
    break;

  case 26: /* underline: T_underline  */
#line 254 "../../lib/common/htmlparse.y"
                        {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1437 "htmlparse.c"
    break;

  case 27: /* n_underline: T_n_underline  */
#line 257 "../../lib/common/htmlparse.y"
                            {popFont(&scanner->parser);}
#line 1443 "htmlparse.c"
    break;

  case 28: /* overline: T_overline  */
#line 260 "../../lib/common/htmlparse.y"
                      {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1449 "htmlparse.c"
    break;

  case 29: /* n_overline: T_n_overline  */
#line 263 "../../lib/common/htmlparse.y"
                          {popFont(&scanner->parser);}
#line 1455 "htmlparse.c"
    break;

  case 30: /* sup: T_sup  */
#line 266 "../../lib/common/htmlparse.y"
            {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1461 "htmlparse.c"
    break;

  case 31: /* n_sup: T_n_sup  */
#line 269 "../../lib/common/htmlparse.y"
                {popFont(&scanner->parser);}
#line 1467 "htmlparse.c"
    break;

  case 32: /* sub: T_sub  */
#line 272 "../../lib/common/htmlparse.y"
            {pushFont(&scanner->parser,(yyvsp[0].font));}
#line 1473 "htmlparse.c"
    break;

  case 33: /* n_sub: T_n_sub  */
#line 275 "../../lib/common/htmlparse.y"
                {popFont(&scanner->parser);}
#line 1479 "htmlparse.c"
    break;

  case 34: /* br: T_br T_end_br  */
#line 278 "../../lib/common/htmlparse.y"
                       { (yyval.i) = (yyvsp[-1].i); }
#line 1485 "htmlparse.c"
    break;

  case 35: /* br: T_BR  */
#line 279 "../../lib/common/htmlparse.y"
              { (yyval.i) = (yyvsp[0].i); }
#line 1491 "htmlparse.c"
    break;

  case 38: /* @1: %empty  */
#line 286 "../../lib/common/htmlparse.y"
                          {
          if (nonSpace(agxbuse(scanner->parser.str))) {
            htmlerror (scanner,"Syntax error: non-space string used before <TABLE>");
            cleanup(&scanner->parser); YYABORT;
          }
          (yyvsp[0].tbl)->prev = scanner->parser.tblstack;
          (yyvsp[0].tbl)->rows = (rows_t){.dtor = free_ritem};
          scanner->parser.tblstack = (yyvsp[0].tbl);
          (yyvsp[0].tbl)->font = *LIST_BACK(&scanner->parser.fontstack);
          (yyval.tbl) = (yyvsp[0].tbl);
        }
#line 1507 "htmlparse.c"
    break;

  case 39: /* table: opt_space T_table @1 rows T_end_table opt_space  */
#line 297 "../../lib/common/htmlparse.y"
                                   {
          if (nonSpace(agxbuse(scanner->parser.str))) {
            htmlerror (scanner,"Syntax error: non-space string used after </TABLE>");
            cleanup(&scanner->parser); YYABORT;
          }
          (yyval.tbl) = scanner->parser.tblstack;
          scanner->parser.tblstack = scanner->parser.tblstack->prev;
        }
#line 1520 "htmlparse.c"
    break;

  case 40: /* fonttable: table  */
#line 307 "../../lib/common/htmlparse.y"
                  { (yyval.tbl) = (yyvsp[0].tbl); }
#line 1526 "htmlparse.c"
    break;

  case 41: /* fonttable: font table n_font  */
#line 308 "../../lib/common/htmlparse.y"
                              { (yyval.tbl)=(yyvsp[-1].tbl); }
#line 1532 "htmlparse.c"
    break;

  case 42: /* fonttable: italic table n_italic  */
#line 309 "../../lib/common/htmlparse.y"
                                  { (yyval.tbl)=(yyvsp[-1].tbl); }
#line 1538 "htmlparse.c"
    break;

  case 43: /* fonttable: underline table n_underline  */
#line 310 "../../lib/common/htmlparse.y"
                                        { (yyval.tbl)=(yyvsp[-1].tbl); }
#line 1544 "htmlparse.c"
    break;

  case 44: /* fonttable: overline table n_overline  */
#line 311 "../../lib/common/htmlparse.y"
                                      { (yyval.tbl)=(yyvsp[-1].tbl); }
#line 1550 "htmlparse.c"
    break;

  case 45: /* fonttable: bold table n_bold  */
#line 312 "../../lib/common/htmlparse.y"
                              { (yyval.tbl)=(yyvsp[-1].tbl); }
#line 1556 "htmlparse.c"
    break;

  case 48: /* rows: row  */
#line 319 "../../lib/common/htmlparse.y"
           { (yyval.p) = (yyvsp[0].p); }
#line 1562 "htmlparse.c"
    break;

  case 49: /* rows: rows row  */
#line 320 "../../lib/common/htmlparse.y"
                { (yyval.p) = (yyvsp[0].p); }
#line 1568 "htmlparse.c"
    break;

  case 50: /* rows: rows HR row  */
#line 321 "../../lib/common/htmlparse.y"
                   { (yyvsp[-2].p)->ruled = true; (yyval.p) = (yyvsp[0].p); }
#line 1574 "htmlparse.c"
    break;

  case 51: /* $@2: %empty  */
#line 324 "../../lib/common/htmlparse.y"
            { addRow (&scanner->parser); }
#line 1580 "htmlparse.c"
    break;

  case 52: /* row: T_row $@2 cells T_end_row  */
#line 324 "../../lib/common/htmlparse.y"
                                                           { (yyval.p) = lastRow(&scanner->parser); }
#line 1586 "htmlparse.c"
    break;

  case 53: /* cells: cell  */
#line 327 "../../lib/common/htmlparse.y"
             { (yyval.cell) = (yyvsp[0].cell); }
#line 1592 "htmlparse.c"
    break;

  case 54: /* cells: cells cell  */
#line 328 "../../lib/common/htmlparse.y"
                   { (yyval.cell) = (yyvsp[0].cell); }
#line 1598 "htmlparse.c"
    break;

  case 55: /* cells: cells VR cell  */
#line 329 "../../lib/common/htmlparse.y"
                      { (yyvsp[-2].cell)->vruled = true; (yyval.cell) = (yyvsp[0].cell); }
#line 1604 "htmlparse.c"
    break;

  case 56: /* $@3: %empty  */
#line 332 "../../lib/common/htmlparse.y"
                        { setCell(&scanner->parser,(yyvsp[-1].cell),(yyvsp[0].tbl),HTML_TBL); }
#line 1610 "htmlparse.c"
    break;

  case 57: /* cell: T_cell fonttable $@3 T_end_cell  */
#line 332 "../../lib/common/htmlparse.y"
                                                                                 { (yyval.cell) = (yyvsp[-3].cell); }
#line 1616 "htmlparse.c"
    break;

  case 58: /* $@4: %empty  */
#line 333 "../../lib/common/htmlparse.y"
                       { setCell(&scanner->parser,(yyvsp[-1].cell),(yyvsp[0].txt),HTML_TEXT); }
#line 1622 "htmlparse.c"
    break;

  case 59: /* cell: T_cell fonttext $@4 T_end_cell  */
#line 333 "../../lib/common/htmlparse.y"
                                                                                 { (yyval.cell) = (yyvsp[-3].cell); }
#line 1628 "htmlparse.c"
    break;

  case 60: /* $@5: %empty  */
#line 334 "../../lib/common/htmlparse.y"
                    { setCell(&scanner->parser,(yyvsp[-1].cell),(yyvsp[0].img),HTML_IMAGE); }
#line 1634 "htmlparse.c"
    break;

  case 61: /* cell: T_cell image $@5 T_end_cell  */
#line 334 "../../lib/common/htmlparse.y"
                                                                               { (yyval.cell) = (yyvsp[-3].cell); }
#line 1640 "htmlparse.c"
    break;

  case 62: /* $@6: %empty  */
#line 335 "../../lib/common/htmlparse.y"
              { setCell(&scanner->parser,(yyvsp[0].cell),mkText(&scanner->parser),HTML_TEXT); }
#line 1646 "htmlparse.c"
    break;

  case 63: /* cell: T_cell $@6 T_end_cell  */
#line 335 "../../lib/common/htmlparse.y"
                                                                                              { (yyval.cell) = (yyvsp[-2].cell); }
#line 1652 "htmlparse.c"
    break;

  case 64: /* image: T_img T_end_img  */
#line 338 "../../lib/common/htmlparse.y"
                         { (yyval.img) = (yyvsp[-1].img); }
#line 1658 "htmlparse.c"
    break;

  case 65: /* image: T_IMG  */
#line 339 "../../lib/common/htmlparse.y"
               { (yyval.img) = (yyvsp[0].img); }
#line 1664 "htmlparse.c"
    break;


#line 1668 "htmlparse.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == HTMLEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (scanner, YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= HTMLEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == HTMLEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, scanner);
          yychar = HTMLEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, scanner);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (scanner, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != HTMLEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, scanner);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, scanner);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 351 "../../lib/common/htmlparse.y"


static void
appendFItemList (htmlparserstate_t *html_state, agxbuf *ag)
{
    const textspan_t ti = {.str = agxbdisown(ag),
                           .font = *LIST_BACK(&html_state->fontstack)};
    LIST_APPEND(&html_state->fitemList, ti);
}

static void
appendFLineList (htmlparserstate_t *html_state, int v)
{
    htextspan_t lp = {0};

    size_t cnt = LIST_SIZE(&html_state->fitemList);
    lp.just = v;
    if (cnt) {
	lp.nitems = cnt;
	lp.items = gv_calloc(cnt, sizeof(textspan_t));

	for (size_t i = 0; i < LIST_SIZE(&html_state->fitemList); ++i) {
	    // move this text span into the new list
	    textspan_t *ti = LIST_AT(&html_state->fitemList, i);
	    lp.items[i] = *ti;
	    *ti = (textspan_t){0};
	}
    }
    else {
	lp.items = gv_alloc(sizeof(textspan_t));
	lp.nitems = 1;
	lp.items[0].str = gv_strdup("");
	lp.items[0].font = *LIST_BACK(&html_state->fontstack);
    }

    LIST_CLEAR(&html_state->fitemList);

    LIST_APPEND(&html_state->fspanList, lp);
}

static htmltxt_t*
mkText(htmlparserstate_t *html_state)
{
    htmltxt_t *hft = gv_alloc(sizeof(htmltxt_t));

    if (!LIST_IS_EMPTY(&html_state->fitemList))
	appendFLineList (html_state, UNSET_ALIGN);

    size_t cnt = LIST_SIZE(&html_state->fspanList);
    hft->nspans = cnt;

    hft->spans = gv_calloc(cnt, sizeof(htextspan_t));
    for (size_t i = 0; i < LIST_SIZE(&html_state->fspanList); ++i) {
    	// move this HTML text span into the new list
    	htextspan_t *hi = LIST_AT(&html_state->fspanList, i);
    	hft->spans[i] = *hi;
    	*hi = (htextspan_t){0};
    }

    LIST_CLEAR(&html_state->fspanList);

    return hft;
}

static row_t *lastRow(htmlparserstate_t *html_state) {
  htmltbl_t* tbl = html_state->tblstack;
  row_t *sp = *LIST_BACK(&tbl->rows);
  return sp;
}

static void addRow(htmlparserstate_t *html_state) {
  htmltbl_t* tbl = html_state->tblstack;
  row_t *sp = gv_alloc(sizeof(row_t));
  if (tbl->hrule)
    sp->ruled = true;
  LIST_APPEND(&tbl->rows, sp);
}

static void setCell(htmlparserstate_t *html_state, htmlcell_t *cp, void *obj, label_type_t kind) {
  htmltbl_t* tbl = html_state->tblstack;
  row_t *rp = *LIST_BACK(&tbl->rows);
  LIST_APPEND(&rp->rp, cp);
  cp->child.kind = kind;
  if (tbl->vrule) {
    cp->vruled = true;
    cp->hruled = false;
  }

  if(kind == HTML_TEXT)
  	cp->child.u.txt = obj;
  else if (kind == HTML_IMAGE)
    cp->child.u.img = obj;
  else
    cp->child.u.tbl = obj;
}

static void cleanup (htmlparserstate_t *html_state)
{
  htmltbl_t* tp = html_state->tblstack;
  htmltbl_t* next;

  if (html_state->lbl) {
    free_html_label (html_state->lbl,1);
    html_state->lbl = NULL;
  }
  while (tp) {
    next = tp->prev;
    cleanTbl (tp);
    tp = next;
  }

  LIST_CLEAR(&html_state->fitemList);
  LIST_CLEAR(&html_state->fspanList);

  LIST_FREE(&html_state->fontstack);
}

static void
pushFont (htmlparserstate_t *html_state, textfont_t *fp)
{
    textfont_t* curfont = *LIST_BACK(&html_state->fontstack);
    textfont_t  f = *fp;

    if (curfont) {
	if (!f.color && curfont->color)
	    f.color = curfont->color;
	if ((f.size < 0.0) && (curfont->size >= 0.0))
	    f.size = curfont->size;
	if (!f.name && curfont->name)
	    f.name = curfont->name;
	if (curfont->flags)
	    f.flags |= curfont->flags;
    }

    textfont_t *const ft = dtinsert(html_state->gvc->textfont_dt, &f);
    LIST_PUSH_BACK(&html_state->fontstack, ft);
}

static void
popFont (htmlparserstate_t *html_state)
{
    LIST_DROP_BACK(&html_state->fontstack);
}

/* Return parsed label or NULL if failure.
 * Set warn to 0 on success; 1 for warning message; 2 if no expat; 3 for error
 * message.
 */
htmllabel_t*
parseHTML (char* txt, int* warn, htmlenv_t *env)
{
  agxbuf        str = {0};
  htmllabel_t*  l = NULL;
  htmlscan_t    scanner = {0};

  LIST_PUSH_BACK(&scanner.parser.fontstack, NULL);
  scanner.parser.fitemList.dtor = free_ti;
  scanner.parser.fspanList.dtor = free_hi;
  scanner.parser.gvc = GD_gvc(env->g);
  scanner.parser.str = &str;

  if (initHTMLlexer (&scanner, txt, &str, env)) {/* failed: no libexpat - give up */
    *warn = 2;
  }
  else {
    htmlparse(&scanner);
    *warn = clearHTMLlexer (&scanner);
    l = scanner.parser.lbl;
  }

  LIST_FREE(&scanner.parser.fitemList);
  LIST_FREE(&scanner.parser.fspanList);

  LIST_FREE(&scanner.parser.fontstack);

  agxbfree (&str);

  return l;
}
