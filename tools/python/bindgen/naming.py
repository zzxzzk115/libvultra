"""doc/lua_api_design.md naming rules, enforced at extraction time.

Lifted from the original gen_lua_bindings.py so the IR can never carry a
non-conformant name into any backend. Modules/usertypes/enums are PascalCase;
functions/methods/properties/fields are camelCase.
"""

from __future__ import annotations

import re
import sys

PASCAL = re.compile(r"^[A-Z][A-Za-z0-9]*$")
CAMEL = re.compile(r"^[a-z][A-Za-z0-9]*$")


def fail(msg: str) -> None:
    print(f"[extract_bindings] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def check_pascal(kind: str, name: str) -> None:
    if not PASCAL.match(name):
        fail(f"{kind} '{name}' is not PascalCase")


def check_camel(owner: str, name: str) -> None:
    """Property/field naming: camelCase, no get/set prefix (spec section 2)."""
    if not CAMEL.match(name):
        fail(f"{owner}.{name}: not camelCase")
    if re.match(r"^(get|set)[A-Z]", name):
        fail(f"{owner}.{name}: get/set prefix on a property/field (spec section 2)")
    if re.search(r"(Euler|Degrees)$", name):
        fail(f"{owner}.{name}: unit-suffixed name; degrees are the default "
             f"(use deprecated= for the legacy alias)")


def check_fn_name(owner: str, name: str) -> None:
    """Function/method naming: camelCase, no unit suffix. get/set prefixes are
    allowed (setX setters and getX/setX symmetric pairs are spec-legal for
    functions; only standalone properties forbid them)."""
    if not CAMEL.match(name):
        fail(f"{owner}.{name}: not camelCase")
    if re.search(r"(Euler|Degrees)$", name):
        fail(f"{owner}.{name}: unit-suffixed name; degrees are the default "
             f"(use deprecated= for the legacy alias)")


def check_struct_field(owner: str, name: str) -> None:
    """Value-struct data fields mirror engine field names: camelCase only.
    Unit suffixes (Degrees/Ms) are legitimate here and these are usertype
    instance members (not enumerated by the conformance checker)."""
    if not CAMEL.match(name):
        fail(f"{owner}.{name}: not camelCase")


def normalize_enum_name(raw: str, strip_e: bool) -> str:
    """Mirror script_binding::normalizeEnumName: drop a leading `e` when the
    next char is uppercase (eSpace -> Space, eKP1 -> KP1, eUnknown -> Unknown).
    Only applied when the enum opted in via `stripE`."""
    if strip_e and len(raw) >= 2 and raw[0] == "e" and raw[1].isupper():
        return raw[1:]
    return raw
