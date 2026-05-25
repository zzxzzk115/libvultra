/// @file
/// @brief data that is shared between the TK renderer and TCL bindings

#pragma once

/// TCL interpreter state
///
/// This is actually declared in TCL’s tcl.h. But we forward declare it here to
/// avoid the TK renderer having a hard dependency on TCL, when it does not use
/// this.
struct Tcl_Interp;

/// context used to convey information between commands and a renderer
typedef struct {
  const char *canvas;        ///< TCL canvas to render to
  struct Tcl_Interp *interp; ///< TCL interpreter
} tcldot_context_t;
