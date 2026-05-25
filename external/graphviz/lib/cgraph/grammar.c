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
#define YYSTYPE         AAGSTYPE
/* Substitute the variable and function names.  */
#define yyparse         aagparse
#define yylex           aaglex
#define yyerror         aagerror
#define yydebug         aagdebug
#define yynerrs         aagnerrs

/* First part of user prologue.  */
#line 61 "../../lib/cgraph/grammar.y"


#include <stdbool.h>
#include <stdio.h>
#include <cghdr.h>
#include <stdlib.h>
#include <util/alloc.h>
#include <util/gv_math.h>
#include <util/streq.h>
#include <util/unreachable.h>

static const char Key[] = "key";

typedef union s {					/* possible items in generic list */
		Agnode_t		*n;
		Agraph_t		*subg;
		Agedge_t		*e;
		Agsym_t			*asym;	/* bound attribute */
		char			*name;	/* unbound attribute */
		struct item_s	*list;	/* list-of-lists (for edgestmt) */
} val_t;

typedef struct item_s {		/* generic list */
	int				tag;	/* T_node, T_subgraph, T_edge, T_attr */
	val_t			u;		/* primary element */
	char			*str;	/* secondary value - port or attr value */
	struct item_s	*next;
} item;

typedef struct list_s {		/* maintain head and tail ptrs for fast append */
	item			*first;
	item			*last;
} list_t;

typedef struct gstack_s {
	Agraph_t *g;
	Agraph_t *subg;
	list_t	nodelist,edgelist,attrlist;
	struct gstack_s *down;
} gstack_t;

/* functions */
static void appendnode(aagscan_t scanner, char *name, char *port, char *sport);
static void attrstmt(aagscan_t scanner, int tkind, char *macroname);
static void startgraph(aagscan_t scanner, char *name, bool directed, bool strict);
static void getedgeitems(aagscan_t scanner);
static void newedge(aagscan_t scanner, Agnode_t *t, char *tport, Agnode_t *h, char *hport, char *key);
static void edgerhs(aagscan_t scanner, Agnode_t *n, char *tport, item *hlist, char *key);
static void appendattr(aagscan_t scanner, char *name, char *value);
static void bindattrs(aagextra_t *ctx, int kind);
static void applyattrs(aagextra_t *ctx, void *obj);
static void endgraph(aagscan_t scanner);
static void endnode(aagscan_t scanner);
static void endedge(aagscan_t scanner);
static void freestack(aagscan_t scanner);
static char* concat(aagscan_t scanner, char*, char*);
static char* concatPort(Agraph_t *G, char*, char*);

static void opensubg(aagscan_t scanner, char *name);
static void closesubg(aagscan_t scanner);
static void graph_error(aagscan_t scanner);


#line 141 "grammar.c"

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

#include "grammar.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_T_graph = 3,                    /* T_graph  */
  YYSYMBOL_T_node = 4,                     /* T_node  */
  YYSYMBOL_T_edge = 5,                     /* T_edge  */
  YYSYMBOL_T_digraph = 6,                  /* T_digraph  */
  YYSYMBOL_T_subgraph = 7,                 /* T_subgraph  */
  YYSYMBOL_T_strict = 8,                   /* T_strict  */
  YYSYMBOL_T_edgeop = 9,                   /* T_edgeop  */
  YYSYMBOL_T_list = 10,                    /* T_list  */
  YYSYMBOL_T_attr = 11,                    /* T_attr  */
  YYSYMBOL_T_atom = 12,                    /* T_atom  */
  YYSYMBOL_T_qatom = 13,                   /* T_qatom  */
  YYSYMBOL_14_ = 14,                       /* '{'  */
  YYSYMBOL_15_ = 15,                       /* '}'  */
  YYSYMBOL_16_ = 16,                       /* ';'  */
  YYSYMBOL_17_ = 17,                       /* ','  */
  YYSYMBOL_18_ = 18,                       /* ':'  */
  YYSYMBOL_19_ = 19,                       /* '='  */
  YYSYMBOL_20_ = 20,                       /* '['  */
  YYSYMBOL_21_ = 21,                       /* ']'  */
  YYSYMBOL_22_ = 22,                       /* '+'  */
  YYSYMBOL_YYACCEPT = 23,                  /* $accept  */
  YYSYMBOL_graph = 24,                     /* graph  */
  YYSYMBOL_body = 25,                      /* body  */
  YYSYMBOL_hdr = 26,                       /* hdr  */
  YYSYMBOL_optgraphname = 27,              /* optgraphname  */
  YYSYMBOL_optstrict = 28,                 /* optstrict  */
  YYSYMBOL_graphtype = 29,                 /* graphtype  */
  YYSYMBOL_optstmtlist = 30,               /* optstmtlist  */
  YYSYMBOL_stmtlist = 31,                  /* stmtlist  */
  YYSYMBOL_optsemi = 32,                   /* optsemi  */
  YYSYMBOL_stmt = 33,                      /* stmt  */
  YYSYMBOL_compound = 34,                  /* compound  */
  YYSYMBOL_simple = 35,                    /* simple  */
  YYSYMBOL_rcompound = 36,                 /* rcompound  */
  YYSYMBOL_37_1 = 37,                      /* $@1  */
  YYSYMBOL_38_2 = 38,                      /* $@2  */
  YYSYMBOL_nodelist = 39,                  /* nodelist  */
  YYSYMBOL_node = 40,                      /* node  */
  YYSYMBOL_attrstmt = 41,                  /* attrstmt  */
  YYSYMBOL_attrtype = 42,                  /* attrtype  */
  YYSYMBOL_optmacroname = 43,              /* optmacroname  */
  YYSYMBOL_optattr = 44,                   /* optattr  */
  YYSYMBOL_attrlist = 45,                  /* attrlist  */
  YYSYMBOL_optattrdefs = 46,               /* optattrdefs  */
  YYSYMBOL_attrdefs = 47,                  /* attrdefs  */
  YYSYMBOL_attrassignment = 48,            /* attrassignment  */
  YYSYMBOL_graphattrdefs = 49,             /* graphattrdefs  */
  YYSYMBOL_subgraph = 50,                  /* subgraph  */
  YYSYMBOL_51_3 = 51,                      /* $@3  */
  YYSYMBOL_optsubghdr = 52,                /* optsubghdr  */
  YYSYMBOL_optseparator = 53,              /* optseparator  */
  YYSYMBOL_atom = 54,                      /* atom  */
  YYSYMBOL_qatom = 55                      /* qatom  */
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
         || (defined AAGSTYPE_IS_TRIVIAL && AAGSTYPE_IS_TRIVIAL)))

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
#define YYFINAL  6
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   59

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  23
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  33
/* YYNRULES -- Number of rules.  */
#define YYNRULES  59
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  76

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   268


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
       2,     2,     2,    22,    17,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    18,    16,
       2,    19,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    20,     2,    21,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    14,     2,    15,     2,     2,     2,     2,
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
       5,     6,     7,     8,     9,    10,    11,    12,    13
};

#if AAGDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,   142,   142,   143,   144,   147,   149,   152,   152,   154,
     154,   156,   156,   158,   158,   160,   160,   162,   162,   164,
     165,   168,   172,   172,   174,   174,   174,   175,   179,   179,
     181,   182,   183,   186,   187,   190,   191,   192,   195,   196,
     199,   199,   201,   203,   204,   206,   209,   212,   215,   215,
     218,   219,   220,   223,   223,   223,   225,   226,   229,   230
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if AAGDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "T_graph", "T_node",
  "T_edge", "T_digraph", "T_subgraph", "T_strict", "T_edgeop", "T_list",
  "T_attr", "T_atom", "T_qatom", "'{'", "'}'", "';'", "','", "':'", "'='",
  "'['", "']'", "'+'", "$accept", "graph", "body", "hdr", "optgraphname",
  "optstrict", "graphtype", "optstmtlist", "stmtlist", "optsemi", "stmt",
  "compound", "simple", "rcompound", "$@1", "$@2", "nodelist", "node",
  "attrstmt", "attrtype", "optmacroname", "optattr", "attrlist",
  "optattrdefs", "attrdefs", "attrassignment", "graphattrdefs", "subgraph",
  "$@3", "optsubghdr", "optseparator", "atom", "qatom", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-18)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-53)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      17,   -18,   -18,    19,     8,     3,   -18,    -2,   -18,   -18,
     -18,     1,   -18,   -18,   -18,     1,   -18,   -18,     9,    -2,
     -18,    18,    21,    23,   -18,    18,     1,   -18,   -18,   -18,
     -18,    10,    13,   -18,   -18,   -18,   -18,   -18,   -18,   -18,
     -18,   -18,     1,   -18,   -18,    22,     8,     1,     1,    25,
      14,    24,   -18,   -18,    27,    24,    26,   -18,   -18,    29,
     -18,   -18,   -18,   -18,     1,    21,    -5,   -18,   -18,   -18,
     -18,    16,    30,   -18,   -18,   -18
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     3,     9,     0,     0,     0,     1,    14,     2,    11,
      12,     8,    35,    36,    37,    51,    56,    58,     0,    13,
      16,    18,    27,    22,    28,    18,    39,    47,    34,    23,
      48,    30,    57,     6,     7,    50,     5,    15,    17,    20,
      24,    41,     0,    19,    41,     0,     0,     0,     0,     0,
      52,    21,    40,    29,    30,     0,    33,    38,    49,    31,
      46,    59,    25,    44,     0,    27,     0,    32,    26,    42,
      43,    55,     0,    53,    54,    45
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -18,   -18,    -4,   -18,   -18,   -18,   -18,   -18,   -18,    31,
      32,   -18,    -7,   -17,   -18,   -18,   -18,    12,   -18,   -18,
     -18,     6,    15,   -18,   -18,   -14,   -18,   -18,   -18,   -18,
     -18,   -11,   -18
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     3,     8,     4,    33,     5,    11,    18,    19,    39,
      20,    21,    22,    41,    50,    65,    23,    24,    25,    26,
      44,    51,    52,    66,    70,    27,    28,    29,    46,    30,
      75,    31,    32
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      34,    12,    13,    14,    35,    15,     9,    16,    17,    10,
      16,    17,   -52,    16,    17,    45,    69,    -4,     1,     6,
     -10,    15,     7,   -10,    36,     2,    16,    17,    47,    48,
      40,    54,    73,    74,    38,    49,    59,    60,    61,    54,
      42,    57,    58,    62,    63,    47,   -40,    64,    68,    48,
      55,    37,    71,    67,    53,    72,    43,     0,     0,    56
};

static const yytype_int8 yycheck[] =
{
      11,     3,     4,     5,    15,     7,     3,    12,    13,     6,
      12,    13,    14,    12,    13,    26,    21,     0,     1,     0,
       3,     7,    14,     6,    15,     8,    12,    13,    18,    19,
       9,    42,    16,    17,    16,    22,    47,    48,    13,    50,
      17,    19,    46,    50,    20,    18,    20,    18,    65,    19,
      44,    19,    66,    64,    42,    66,    25,    -1,    -1,    44
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     1,     8,    24,    26,    28,     0,    14,    25,     3,
       6,    29,     3,     4,     5,     7,    12,    13,    30,    31,
      33,    34,    35,    39,    40,    41,    42,    48,    49,    50,
      52,    54,    55,    27,    54,    54,    15,    33,    16,    32,
       9,    36,    17,    32,    43,    54,    51,    18,    19,    22,
      37,    44,    45,    40,    54,    44,    45,    19,    25,    54,
      54,    13,    35,    20,    18,    38,    46,    54,    36,    21,
      47,    48,    54,    16,    17,    53
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    23,    24,    24,    24,    25,    26,    27,    27,    28,
      28,    29,    29,    30,    30,    31,    31,    32,    32,    33,
      33,    34,    35,    35,    37,    38,    36,    36,    39,    39,
      40,    40,    40,    41,    41,    42,    42,    42,    43,    43,
      44,    44,    45,    46,    46,    47,    48,    49,    51,    50,
      52,    52,    52,    53,    53,    53,    54,    54,    55,    55
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     0,     3,     3,     1,     0,     1,
       0,     1,     1,     1,     0,     2,     1,     1,     0,     2,
       2,     3,     1,     1,     0,     0,     5,     0,     1,     3,
       1,     3,     5,     3,     1,     1,     1,     1,     2,     0,
       1,     0,     4,     2,     0,     2,     3,     1,     0,     3,
       2,     1,     0,     1,     1,     0,     1,     1,     1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = AAGEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == AAGEMPTY)                                        \
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
   Use AAGerror or AAGUNDEF. */
#define YYERRCODE AAGUNDEF


/* Enable debugging if requested.  */
#if AAGDEBUG

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
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, aagscan_t scanner)
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
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, aagscan_t scanner)
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
                 int yyrule, aagscan_t scanner)
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
#else /* !AAGDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !AAGDEBUG */


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
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, aagscan_t scanner)
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
yyparse (aagscan_t scanner)
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

  yychar = AAGEMPTY; /* Cause a token to be read.  */

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
  if (yychar == AAGEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, scanner);
    }

  if (yychar <= AAGEOF)
    {
      yychar = AAGEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == AAGerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = AAGUNDEF;
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
  yychar = AAGEMPTY;
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
  case 2: /* graph: hdr body  */
#line 142 "../../lib/cgraph/grammar.y"
                            {freestack(scanner); endgraph(scanner);}
#line 1237 "grammar.c"
    break;

  case 3: /* graph: error  */
#line 143 "../../lib/cgraph/grammar.y"
                                        {graph_error(scanner);}
#line 1243 "grammar.c"
    break;

  case 6: /* hdr: optstrict graphtype optgraphname  */
#line 149 "../../lib/cgraph/grammar.y"
                                                                 {startgraph(scanner,(yyvsp[0].str),(yyvsp[-1].i) != 0,(yyvsp[-2].i) != 0);}
#line 1249 "grammar.c"
    break;

  case 7: /* optgraphname: atom  */
#line 152 "../../lib/cgraph/grammar.y"
                     {(yyval.str)=(yyvsp[0].str);}
#line 1255 "grammar.c"
    break;

  case 8: /* optgraphname: %empty  */
#line 152 "../../lib/cgraph/grammar.y"
                                            {(yyval.str)=0;}
#line 1261 "grammar.c"
    break;

  case 9: /* optstrict: T_strict  */
#line 154 "../../lib/cgraph/grammar.y"
                             {(yyval.i)=1;}
#line 1267 "grammar.c"
    break;

  case 10: /* optstrict: %empty  */
#line 154 "../../lib/cgraph/grammar.y"
                                                    {(yyval.i)=0;}
#line 1273 "grammar.c"
    break;

  case 11: /* graphtype: T_graph  */
#line 156 "../../lib/cgraph/grammar.y"
                                {(yyval.i) = 0;}
#line 1279 "grammar.c"
    break;

  case 12: /* graphtype: T_digraph  */
#line 156 "../../lib/cgraph/grammar.y"
                                                          {(yyval.i) = 1;}
#line 1285 "grammar.c"
    break;

  case 21: /* compound: simple rcompound optattr  */
#line 169 "../../lib/cgraph/grammar.y"
                                        {if ((yyvsp[-1].i)) endedge(scanner); else endnode(scanner);}
#line 1291 "grammar.c"
    break;

  case 24: /* $@1: %empty  */
#line 174 "../../lib/cgraph/grammar.y"
                                 {getedgeitems(scanner);}
#line 1297 "grammar.c"
    break;

  case 25: /* $@2: %empty  */
#line 174 "../../lib/cgraph/grammar.y"
                                                                 {getedgeitems(scanner);}
#line 1303 "grammar.c"
    break;

  case 26: /* rcompound: T_edgeop $@1 simple $@2 rcompound  */
#line 174 "../../lib/cgraph/grammar.y"
                                                                                                    {(yyval.i) = 1;}
#line 1309 "grammar.c"
    break;

  case 27: /* rcompound: %empty  */
#line 175 "../../lib/cgraph/grammar.y"
                                            {(yyval.i) = 0;}
#line 1315 "grammar.c"
    break;

  case 30: /* node: atom  */
#line 181 "../../lib/cgraph/grammar.y"
                       {appendnode(scanner,(yyvsp[0].str),NULL,NULL);}
#line 1321 "grammar.c"
    break;

  case 31: /* node: atom ':' atom  */
#line 182 "../../lib/cgraph/grammar.y"
                            {appendnode(scanner,(yyvsp[-2].str),(yyvsp[0].str),NULL);}
#line 1327 "grammar.c"
    break;

  case 32: /* node: atom ':' atom ':' atom  */
#line 183 "../../lib/cgraph/grammar.y"
                                     {appendnode(scanner,(yyvsp[-4].str),(yyvsp[-2].str),(yyvsp[0].str));}
#line 1333 "grammar.c"
    break;

  case 33: /* attrstmt: attrtype optmacroname attrlist  */
#line 186 "../../lib/cgraph/grammar.y"
                                                  {attrstmt(scanner,(yyvsp[-2].i),(yyvsp[-1].str));}
#line 1339 "grammar.c"
    break;

  case 34: /* attrstmt: graphattrdefs  */
#line 187 "../../lib/cgraph/grammar.y"
                                         {attrstmt(scanner,T_graph,NULL);}
#line 1345 "grammar.c"
    break;

  case 35: /* attrtype: T_graph  */
#line 190 "../../lib/cgraph/grammar.y"
                        {(yyval.i) = T_graph;}
#line 1351 "grammar.c"
    break;

  case 36: /* attrtype: T_node  */
#line 191 "../../lib/cgraph/grammar.y"
                                 {(yyval.i) = T_node;}
#line 1357 "grammar.c"
    break;

  case 37: /* attrtype: T_edge  */
#line 192 "../../lib/cgraph/grammar.y"
                                 {(yyval.i) = T_edge;}
#line 1363 "grammar.c"
    break;

  case 38: /* optmacroname: atom '='  */
#line 195 "../../lib/cgraph/grammar.y"
                        {(yyval.str) = (yyvsp[-1].str);}
#line 1369 "grammar.c"
    break;

  case 39: /* optmacroname: %empty  */
#line 196 "../../lib/cgraph/grammar.y"
                                      {(yyval.str) = NULL; }
#line 1375 "grammar.c"
    break;

  case 46: /* attrassignment: atom '=' atom  */
#line 209 "../../lib/cgraph/grammar.y"
                                 {appendattr(scanner,(yyvsp[-2].str),(yyvsp[0].str));}
#line 1381 "grammar.c"
    break;

  case 48: /* $@3: %empty  */
#line 215 "../../lib/cgraph/grammar.y"
                              {opensubg(scanner,(yyvsp[0].str));}
#line 1387 "grammar.c"
    break;

  case 49: /* subgraph: optsubghdr $@3 body  */
#line 215 "../../lib/cgraph/grammar.y"
                                                           {closesubg(scanner);}
#line 1393 "grammar.c"
    break;

  case 50: /* optsubghdr: T_subgraph atom  */
#line 218 "../../lib/cgraph/grammar.y"
                                  {(yyval.str)=(yyvsp[0].str);}
#line 1399 "grammar.c"
    break;

  case 51: /* optsubghdr: T_subgraph  */
#line 219 "../../lib/cgraph/grammar.y"
                                      {(yyval.str)=NULL;}
#line 1405 "grammar.c"
    break;

  case 52: /* optsubghdr: %empty  */
#line 220 "../../lib/cgraph/grammar.y"
                                      {(yyval.str)=NULL;}
#line 1411 "grammar.c"
    break;

  case 56: /* atom: T_atom  */
#line 225 "../../lib/cgraph/grammar.y"
                  {(yyval.str) = (yyvsp[0].str);}
#line 1417 "grammar.c"
    break;

  case 57: /* atom: qatom  */
#line 226 "../../lib/cgraph/grammar.y"
                                 {(yyval.str) = (yyvsp[0].str);}
#line 1423 "grammar.c"
    break;

  case 58: /* qatom: T_qatom  */
#line 229 "../../lib/cgraph/grammar.y"
                   {(yyval.str) = (yyvsp[0].str);}
#line 1429 "grammar.c"
    break;

  case 59: /* qatom: qatom '+' T_qatom  */
#line 230 "../../lib/cgraph/grammar.y"
                                             {(yyval.str) = concat(scanner, (yyvsp[-2].str),(yyvsp[0].str));}
#line 1435 "grammar.c"
    break;


#line 1439 "grammar.c"

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
  yytoken = yychar == AAGEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
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

      if (yychar <= AAGEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == AAGEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, scanner);
          yychar = AAGEMPTY;
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
  if (yychar != AAGEMPTY)
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

#line 232 "../../lib/cgraph/grammar.y"


static item *newitem(int tag, void *p0, char *p1)
{
	item *rv = gv_alloc(sizeof(item));
	rv->tag = tag;
	rv->u.name = p0;
	rv->str = p1;
	return rv;
}

static item *cons_node(Agnode_t *n, char *port)
	{ return newitem(T_node,n,port); }

static item *cons_attr(char *name, char *value)
	{ return newitem(T_atom,name,value); }

static item *cons_list(item *list)
	{ return newitem(T_list,list,NULL); }

static item *cons_subg(Agraph_t *subg)
	{ return newitem(T_subgraph,subg,NULL); }

static gstack_t *push(gstack_t *s, Agraph_t *subg) {
	gstack_t *rv = gv_alloc(sizeof(gstack_t));
	rv->down = s;
	rv->g = subg;
	return rv;
}

static gstack_t *pop(gstack_t *s)
{
	gstack_t *rv;
	rv = s->down;
	free(s);
	return rv;
}

static void delete_items(Agraph_t *G, item *ilist)
{
	item	*p,*pn;

	for (p = ilist; p; p = pn) {
		pn = p->next;
		if (p->tag == T_list) delete_items(G, p->u.list);
		if (p->tag == T_atom) agstrfree(G, p->str, aghtmlstr(p->str));
		free(p);
	}
}

static void deletelist(Agraph_t *G, list_t *list)
{
	delete_items(G,list->first);
	list->first = list->last = NULL;
}

static void listapp(list_t *list, item *v)
{
	if (list->last) list->last->next = v;
	list->last = v;
	if (list->first == NULL) list->first = v;
}


/* attrs */
static void appendattr(aagscan_t scanner, char *name, char *value)
{
	item		*v;
	aagextra_t 	*ctx = aagget_extra(scanner);

	assert(value != NULL);
	v = cons_attr(name,value);
	listapp(&ctx->S->attrlist, v);
}

static void bindattrs(aagextra_t *ctx, int kind)
{
	item		*aptr;
	char		*name;

	for (aptr = ctx->S->attrlist.first; aptr; aptr = aptr->next) {
		assert(aptr->tag == T_atom);	/* signifies unbound attr */
		name = aptr->u.name;
		if (kind == AGEDGE && streq(name,Key)) continue;
		if ((aptr->u.asym = agattr_text(ctx->S->g,kind,name,NULL)) == NULL)
			aptr->u.asym = agattr_text(ctx->S->g,kind,name,"");
		aptr->tag = T_attr;				/* signifies bound attr */
		agstrfree(ctx->G, name, false);
	}
}

/* attach node/edge specific attributes */
static void applyattrs(aagextra_t *ctx, void *obj)
{
	item		*aptr;

	for (aptr = ctx->S->attrlist.first; aptr; aptr = aptr->next) {
		if (aptr->tag == T_attr) {
			if (aptr->u.asym) {
				if (aghtmlstr(aptr->str)) {
				  agxset_html(obj, aptr->u.asym, aptr->str);
				} else {
				  agxset(obj, aptr->u.asym, aptr->str);
				}
			}
		}
		else {
			assert(AGTYPE(obj) == AGINEDGE || AGTYPE(obj) == AGOUTEDGE);
			assert(aptr->tag == T_atom);
			assert(streq(aptr->u.name,Key));
		}
	}
}

static void nomacros(void)
{
  agwarningf("attribute macros not implemented");
}

/* attrstmt:
 * First argument is always attrtype, so switch covers all cases.
 * This function is used to handle default attribute value assignment.
 */
static void attrstmt(aagscan_t scanner, int tkind, char *macroname)
{
	item			*aptr;
	int				kind = 0;
	aagextra_t 		*ctx = aagget_extra(scanner);
	Agsym_t*  sym;
	Agraph_t *G = ctx->G;
	gstack_t *S = ctx->S;

		/* creating a macro def */
	if (macroname) nomacros();
		/* invoking a macro def */
	for (aptr = S->attrlist.first; aptr; aptr = aptr->next)
		if (aptr->str == NULL) nomacros();

	switch(tkind) {
		case T_graph: kind = AGRAPH; break;
		case T_node: kind = AGNODE; break;
		case T_edge: kind = AGEDGE; break;
		default: UNREACHABLE();
	}
	bindattrs(ctx, kind);	/* set up defaults for new attributes */
	for (aptr = S->attrlist.first; aptr; aptr = aptr->next) {
		/* If the tag is still T_atom, aptr->u.asym has not been set */
		if (aptr->tag == T_atom) continue;
		if (!aptr->u.asym->fixed || S->g != G) {
			if (aghtmlstr(aptr->str)) {
			  sym = agattr_html(S->g, kind, aptr->u.asym->name, aptr->str);
			} else {
			  sym = agattr_text(S->g, kind, aptr->u.asym->name, aptr->str);
			}
		} else
			sym = aptr->u.asym;
		if (S->g == G)
			sym->print = true;
	}
	deletelist(G, &S->attrlist);
}

/* nodes */

static void appendnode(aagscan_t scanner, char *name, char *port, char *sport)
{
	item		*elt;
	aagextra_t 	*ctx = aagget_extra(scanner);
	Agraph_t *G = ctx->G;
	gstack_t *S = ctx->S;

	if (sport) {
		port = concatPort (G, port, sport);
	}
	elt = cons_node(agnode(S->g, name, 1), port);
	listapp(&S->nodelist, elt);
	agstrfree(G, name, false);
}

/* apply current optional attrs to nodelist and clean up lists */
/* what's bad is that this could also be endsubg.  also, you can't
clean up S->subg in closesubg() because S->subg might be needed
to construct edges.  these are the sort of notes you write to yourself
in the future. */
static void endnode(aagscan_t scanner)
{
	item	*ptr;
	aagextra_t 	*ctx = aagget_extra(scanner);
	Agraph_t *G = ctx->G;
	gstack_t *S = ctx->S;

	bindattrs(ctx, AGNODE);
	for (ptr = S->nodelist.first; ptr; ptr = ptr->next)
		applyattrs(ctx, ptr->u.n);
	deletelist(G, &S->nodelist);
	deletelist(G, &S->attrlist);
	deletelist(G, &S->edgelist);
	S->subg = 0;  /* notice a pattern here? :-( */
}

/* edges - store up node/subg lists until optional edge key can be seen */

static void getedgeitems(aagscan_t scanner)
{
	aagextra_t *ctx = aagget_extra(scanner);;
	gstack_t *S = ctx->S;
	item	*v = 0;

	if (S->nodelist.first) {
		v = cons_list(S->nodelist.first);
		S->nodelist.first = S->nodelist.last = NULL;
	}
	else {if (S->subg) v = cons_subg(S->subg); S->subg = 0;}
	/* else nil append */
	if (v) listapp(&S->edgelist, v);
}

static void endedge(aagscan_t scanner)
{
	char			*key;
	item			*aptr,*tptr,*p;

	Agnode_t		*t;
	Agraph_t		*subg;
	aagextra_t 		*ctx = aagget_extra(scanner);
	Agraph_t 		*G = ctx->G;

	bindattrs(ctx, AGEDGE);

	/* look for "key" pseudo-attribute */
	key = NULL;
	for (aptr = ctx->S->attrlist.first; aptr; aptr = aptr->next) {
		if (aptr->tag == T_atom && streq(aptr->u.name,Key))
			key = aptr->str;
	}

	/* can make edges with node lists or subgraphs */
	for (p = ctx->S->edgelist.first; p->next; p = p->next) {
		if (p->tag == T_subgraph) {
			subg = p->u.subg;
			for (t = agfstnode(subg); t; t = agnxtnode(subg,t))
				edgerhs(scanner,agsubnode(ctx->S->g, t, 0), NULL, p->next, key);
		}
		else {
			for (tptr = p->u.list; tptr; tptr = tptr->next)
				edgerhs(scanner,tptr->u.n,tptr->str,p->next,key);
		}
	}
	deletelist(G, &ctx->S->nodelist);
	deletelist(G, &ctx->S->edgelist);
	deletelist(G, &ctx->S->attrlist);
	ctx->S->subg = 0;
}

/* concat:
 */
static char*
concat (aagscan_t scanner, char* s1, char* s2)
{
  agxbuf buf = {0};
  Agraph_t *G = aagget_extra(scanner)->G;

  agxbprint(&buf, "%s%s", s1, s2);
  char *const s = agstrdup(G, agxbuse(&buf));
  agstrfree(G, s1, false);
  agstrfree(G, s2, false);
  agxbfree(&buf);
  return s;
}

static char*
concatPort (Agraph_t *G, char* s1, char* s2)
{
  agxbuf buf = {0};

  agxbprint(&buf, "%s:%s", s1, s2);
  char *s = agstrdup(G, agxbuse(&buf));
  agstrfree(G, s1, false);
  agstrfree(G, s2, false);
  agxbfree(&buf);
  return s;
}


static void edgerhs(aagscan_t scanner, Agnode_t *tail, char *tport, item *hlist, char *key)
{
	Agnode_t		*head;
	Agraph_t		*subg;
	item			*hptr;
	aagextra_t 		*ctx = aagget_extra(scanner);

	if (hlist->tag == T_subgraph) {
		subg = hlist->u.subg;
		for (head = agfstnode(subg); head; head = agnxtnode(subg,head))
			newedge(scanner, tail, tport, agsubnode(ctx->S->g, head, 0), NULL, key);
	}
	else {
		for (hptr = hlist->u.list; hptr; hptr = hptr->next)
			newedge(scanner, tail, tport, agsubnode(ctx->S->g, hptr->u.n, 0), hptr->str, key);
	}
}

static void mkport(aagscan_t scanner, Agedge_t *e, char *name, char *val)
{
	Agsym_t *attr;
	aagextra_t *ctx = aagget_extra(scanner);

	if (val) {
		if ((attr = agattr_text(ctx->S->g,AGEDGE,name,NULL)) == NULL)
			attr = agattr_text(ctx->S->g,AGEDGE,name,"");
		agxset(e,attr,val);
	}
}

static void newedge(aagscan_t scanner, Agnode_t *t, char *tport, Agnode_t *h, char *hport, char *key)
{
	Agedge_t 	*e;
	aagextra_t 	*ctx = aagget_extra(scanner);

	e = agedge(ctx->S->g, t, h, key, 1);
	if (e) {		/* can fail if graph is strict and t==h */
		char    *tp = tport;
		char    *hp = hport;
		if (agtail(e) != aghead(e) && aghead(e) == t) {
			/* could happen with an undirected edge */
			SWAP(&tp, &hp);
		}
		mkport(scanner, e,TAILPORT_ID,tp);
		mkport(scanner, e,HEADPORT_ID,hp);
		applyattrs(ctx, e);
	}
}

/* graphs and subgraphs */


static void startgraph(aagscan_t scanner, char *name, bool directed, bool strict)
{
	aagextra_t *ctx = aagget_extra(scanner);
	if (ctx->G == NULL) {
		ctx->SubgraphDepth = 0;
		Agdesc_t req = {.directed = directed, .strict = strict, .maingraph = true};
		ctx->G = agopen(name,req,ctx->Disc);
	}
	ctx->S = push(ctx->S,ctx->G);
	agstrfree(NULL, name, false);
}

static void endgraph(aagscan_t scanner)
{
	aglexeof(scanner);
	aginternalmapclearlocalnames(aagget_extra(scanner)->G);
}

static void opensubg(aagscan_t scanner, char *name)
{
  aagextra_t *ctx = aagget_extra(scanner);

  if (++ctx->SubgraphDepth >= YYMAXDEPTH/2) {
    agerrorf("subgraphs nested more than %d deep", YYMAXDEPTH);
  }
	ctx->S = push(ctx->S, agsubg(ctx->S->g, name, 1));
	agstrfree(ctx->G, name, false);
}

static void closesubg(aagscan_t scanner)
{
	aagextra_t *ctx = aagget_extra(scanner);
	Agraph_t *subg = ctx->S->g;

	--ctx->SubgraphDepth;
	ctx->S = pop(ctx->S);
	ctx->S->subg = subg;
	assert(subg);
}

static void freestack(aagscan_t scanner)
{
	aagextra_t *ctx = aagget_extra(scanner);
	while (ctx->S) {
		deletelist(ctx->G, &ctx->S->nodelist);
		deletelist(ctx->G, &ctx->S->attrlist);
		deletelist(ctx->G, &ctx->S->edgelist);
		ctx->S = pop(ctx->S);
	}
}

static void graph_error(aagscan_t scanner)
{
	aagextra_t *ctx = aagget_extra(scanner);
	if (ctx->G) {
		freestack(scanner);
		endgraph(scanner);
		agclose(ctx->G);
		ctx->G = NULL;
	}
}

Agraph_t *agconcat(Agraph_t *g, const char *filename, void *chan,
                   Agdisc_t *disc) {
	aagscan_t scanner = NULL;
	aagextra_t extra = {
		.Disc = disc ? disc : &AgDefaultDisc,
		.Ifile = chan,
		.G = g,
		.line_num = 1,
		.InputFile = filename,
	};
	if (aaglex_init_extra(&extra, &scanner)) {
		return NULL;
	}
	aagset_in(chan, scanner);
	aagparse(scanner);
	if (extra.G == NULL) aglexbad(scanner);
	aaglex_destroy(scanner);
	agxbfree(&extra.InputFileBuffer);
	agxbfree(&extra.Sbuf);
	return extra.G;
}

Agraph_t *agread(void *fp, Agdisc_t *disc) {
  return agconcat(NULL, NULL, fp, disc);
}

