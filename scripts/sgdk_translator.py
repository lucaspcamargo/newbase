#!/usr/bin/env python3
"""
sgdk_translator.py

Transforms SGDK C headers and sources for use with the newbase SGDK
compatibility layer (NEWBASE_SGDK builds).

Two passes:
  1. Header pass  — wraps hardware I/O macro definitions in
                    #ifndef NEWBASE_SGDK guards so the host build
                    can override them with its own implementations.

  2. Source pass  — rewrites direct VDP/hardware port dereferences
                    (e.g.  *(volatile u32*)VDP_CTRL_PORT = val)
                    to calls into the PAL shim
                    (e.g.  vdp_write_ctrl(val)).

The tool is data-driven: the lists of hardware macros and port
rewrite rules live here, not in patched SGDK files.  When SGDK
updates, re-run the tool.

Usage:
    python3 sgdk_translator.py --sgdk-root path/to/SGDK \
                               --out-root  path/to/build/sgdk_wrapped

    # Or process a single file for debugging:
    python3 sgdk_translator.py --file path/to/vdp.h --out-root /tmp/wrapped
"""

import argparse
import importlib.util
import re
from pathlib import Path

# to
SEARCH_ROOTS = ["inc", "src"]
EXCLUDE_DIRS = []
INCLUDE_EXTENSIONS = ["h", "c"]


# ---------------------------------------------------------------------------
# Data: specific transformations to apply to specific files
# ---------------------------------------------------------------------------

# More general transformations to apply to specific files
# Transformation types:
# + rm_func_def: removes function definitions from the source body
# + middle_extension: adds a component to the file's extension (for example maths.c could become maths.patched.c)
# + rm_defines: removes all defines corresponding to the given identifiers
# + change_typedefs: change the origin type for all typedefs corresponding to an identifier. "diff" array parameter receives tuples of (identifier, new_origin_type)

TRANSFORMS = {
    "inc/asm.h": [
        {"do":"rm_defines", "ids": [
            "VAR2REG_B", "VAR2REG_W", "VAR2REG_L",
            "REG2VAR_B", "REG2VAR_W", "REG2VAR_L",
        ]}
    ],
    "inc/config.h": [
        {"do": "change_defines", "mapping": [
            ("MODULE_FLASHSAVE", 0),
            ("MODULE_CONSOLE", 0),
        ]},
    ],
    "inc/types.h": [
        {"do": "prepend_includes", "includes": [
            "\"stdint.h\"", "\"stddef.h\""
        ]},
        {"do":"rm_defines", "ids": [
            "uint8_t", "int8_t",
            "uint16_t", "int16_t",
            "uint32_t", "int32_t",
            "size_t", 
            "ptrdiff_t",
            
        ]},
        {"do": "rm_typedefs", "ids": [
            "size_t"
        ]},
        {"do": "change_typedefs", "diff": [
            ("u8", "uint8_t"),
            ("u16", "uint16_t"),
            ("u32", "uint32_t"),
            ("s8", "int8_t"),
            ("s16", "int16_t"),
            ("s32", "int32_t"),
        ]}
    ],
    "inc/vdp.h": [
        {"do": "rm_defines", "ids": [
            "GET_VDP_STATUS"
        ]}
    ],
    "inc/string.h": [
        {"do": "prepend_includes", "includes": [
            "\"stdarg.h\""
        ]},
        {"do": "rm_typedefs", "ids": [
            "__gnuc_va_list", "va_list" # use from stdarg.h
        ]},
        {"do": "rm_defines", "ids": [
            "va_start", "va_end", "va_arg" # use from stdarg.h
        ]},
        {"do":"rm_func_decl", "ids":[
            "sprintf", "vsprintf"  # weird va_list usage, better use stdlib
        ]},
    ],
    "src/maths.c": [
        {"do":"rm_func_def", "ids":[
            "divu", "divs", "modu", "mods", "divmodu", "divmods"  # asm
        ]},
        {"do":"middle_extension", "ext":"patched"}
    ],
    "src/pal.c" : [
        {"do":"rm_func_def", "ids":[
            "PAL_getColors", "PAL_getPalette"  # io port access and endianness
        ]}
    ],
    "src/string.c": [
        {"do":"rm_func_def", "ids":[
            "sprintf", "vsprintf"  # weird va_list usage, better use stdlib
        ]}
    ],
    "src/vdp_bg.c": [
        {"do":"rm_func_def", "ids":[
            "VDP_setVerticalScroll",  # mmio port usage
            "VDP_setHorizontalScroll",
            "VDP_waitVBlank",
            "VDP_doVBlankScrollProcess",
        ]}        
    ],
    "src/vdp_tile.c": [
        {"do":"rm_func_def", "ids":[
            "VDP_fillTileMap",  # mmio port usage
            "VDP_setTileMapDataEx",
            "VDP_setTileMapXY",
            "VDP_fillTileMapRect",
            "VDP_fillTileMapRectInc",
            "VDP_setTileMapDataColumn",
            "VDP_setTileMapDataColumnEx",
            "setTileMapDataColumn",
            "setTileMapDataColumnEx",
        ]},
        {"do":"func_def_not_static", "ids":[
            "prepareTileMapDataRowEx",
            "prepareTileMapDataColumn",
            "prepareTileMapDataColumnEx",
        ]},
    ]
}



# ---------------------------------------------------------------------------
# Data: hardware I/O macros that must be guarded in headers
# ---------------------------------------------------------------------------

# Macros whose definitions should be wrapped with #ifndef NEWBASE_SGDK.
# The host PAL provides its own definitions for these.
HARDWARE_MACROS = {
    # VDP ports
    "VDP_DATA_PORT",
    "VDP_CTRL_PORT",
    "VDP_HV_COUNTER",
    "GFX_DATA_PORT",
    "GFX_CTRL_PORT",
    # Z80
    "Z80_RAM",
    "Z80_BUSREQ",
    "Z80_RESET",
    # YM2612
    "YM2612_A0",
    "YM2612_D0",
    "YM2612_A1",
    "YM2612_D1",
    # PSG
    "PSG_PORT",
    # I/O
    "IO_DATA_1",
    "IO_DATA_2",
    "IO_CTRL_1",
    "IO_CTRL_2",
}


# ---------------------------------------------------------------------------
# Data: port-dereference rewrite rules
# ---------------------------------------------------------------------------
# Each rule matches a cast-and-deref pattern and maps it to a PAL function.
#
# Pattern (written, read):
#   *(volatile TYPE *) PORT_MACRO = val   ->  write_fn(val)
#   val = *(volatile TYPE *) PORT_MACRO   ->  read_fn()

PORT_RULES = [
    {
        "macros":    {"VDP_CTRL_PORT", "GFX_CTRL_PORT"},
        "ptr_type":  "u16",          # matches u16, vu16, volatile u16
        "write_fn":  "vdp_write_ctrl",
        "read_fn":   "vdp_read_ctrl",
    },
    {
        "macros":    {"VDP_DATA_PORT", "GFX_DATA_PORT"},
        "ptr_type":  "u16",
        "write_fn":  "vdp_write_data",
        "read_fn":   "vdp_read_data",
    },
    {
        "macros":    {"VDP_HV_COUNTER"},
        "ptr_type":  "u16",
        "write_fn":  None,           # read-only port
        "read_fn":   "vdp_read_hvcounter",
    },
    {
        "macros":    {"Z80_BUSREQ"},
        "ptr_type":  "u16",
        "write_fn":  "z80_write_busreq",
        "read_fn":   "z80_read_busreq",
    },
    {
        "macros":    {"Z80_RESET"},
        "ptr_type":  "u16",
        "write_fn":  "z80_write_reset",
        "read_fn":   None,
    },
]

# Build a flat macro -> rule lookup
_MACRO_TO_RULE: dict[str, dict] = {}
for _rule in PORT_RULES:
    for _m in _rule["macros"]:
        _MACRO_TO_RULE[_m] = _rule


# ---------------------------------------------------------------------------
# Pass 1: Header guard transformation (text-level, regex-based)
# ---------------------------------------------------------------------------
# We don't not handle preprocessor directives, so we operate on the
# raw text for the header pass.  The pattern is straightforward enough that
# a line-oriented approach is reliable.

_DEFINE_RE   = re.compile(r"^\s*#\s*define\s+(\w+)")
_COMMENT_RE  = re.compile(r"^\s*(?://|/\*|\*)")   # line-comment, block-comment open/body


def _prepend_includes(text: str, includes: list[str]) -> str:
    """Insert ``#include`` directives after any leading comment block.

    ``includes`` entries are the verbatim token after ``#include``, e.g.
    ``"<stdint.h>"`` or ``'"myheader.h"'``.
    """
    lines = text.splitlines(keepends=True)

    # Find the first line that is not a blank line or part of a comment block.
    insert_at = 0
    in_block = False
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if in_block:
            if "*/" in line:
                in_block = False
            insert_at = idx + 1
        elif not stripped:
            insert_at = idx + 1          # keep leading blank lines above
        elif _COMMENT_RE.match(line):
            if "/*" in line and "*/" not in line:
                in_block = True
            insert_at = idx + 1
        else:
            break                        # first real content line

    injection = "".join(f"#include {inc}\n" for inc in includes)
    return "".join(lines[:insert_at]) + injection + "".join(lines[insert_at:])


def _remove_defines(source_text: str, ids: list[str]) -> str:
    """Remove ``#define`` directives for the given identifiers.

    Handles single-line and backslash-continued multi-line macros.
    """
    id_set = set(ids)
    lines = source_text.splitlines(keepends=True)
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = _DEFINE_RE.match(line)
        if m and m.group(1) in id_set:
            # Skip the full macro body (may span multiple lines via \)
            while True:
                cont = lines[i].rstrip("\n\r").endswith("\\")
                i += 1
                if not cont:
                    break
        else:
            out.append(line)
            i += 1
    return "".join(out)


def _guard_hardware_macros(source_text: str) -> str:
    """
    Wrap each #define of a known hardware macro with:

        #ifndef NEWBASE_SGDK
        #define FOO ...
        #endif

    Handles single-line and backslash-continued multi-line macros.
    """
    lines = source_text.splitlines(keepends=True)
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = _DEFINE_RE.match(line)
        if m and m.group(1) in HARDWARE_MACROS:
            # Collect the full macro (may span multiple lines via \)
            macro_lines = []
            while True:
                macro_lines.append(lines[i])
                if not lines[i].rstrip("\n\r").endswith("\\"):
                    i += 1
                    break
                i += 1

            out.append("#ifndef NEWBASE_SGDK\n")
            out.extend(macro_lines)
            out.append("#endif /* NEWBASE_SGDK */\n")
        else:
            out.append(line)
            i += 1

    return "".join(out)


# ---------------------------------------------------------------------------
# Pass 2: Tree-sitter based port dereference detection and rewriting
# ---------------------------------------------------------------------------
# tree-sitter parses the raw C text, so macro names like VDP_CTRL_PORT are
# still visible as identifier nodes — which is exactly what we need.
#
# Two patterns are rewritten automatically:
#   A. Direct deref write:  *((TYPE*) PORT) = val  →  write_fn(val)
#   B. Direct deref read:   x = *((TYPE*) PORT)    →  x = read_fn()
#
# Pattern C (ptr-assign):  pw = (TYPE*) PORT; *pw = val;
#   — Requires inter-statement data-flow analysis. Detected and warned,
#     but not automatically rewritten (manual fix needed).

def _ts_available() -> bool:
    return (importlib.util.find_spec("tree_sitter") is not None and
            importlib.util.find_spec("tree_sitter_c") is not None)


_TS_PARSER = None
_TS_LANG   = None


def _ts_get_parser():
    global _TS_PARSER, _TS_LANG
    if _TS_PARSER is None:
        from tree_sitter import Language, Parser
        import tree_sitter_c as _tsc
        _TS_LANG   = Language(_tsc.language())
        _TS_PARSER = Parser(_TS_LANG)
    return _TS_PARSER, _TS_LANG


def _deref_port_id(node):
    """
    If *node* is a ``pointer_expression`` that dereferences a port cast,
    return the identifier node for the port macro name; otherwise None.

    Handles:
      *((TYPE*) PORT)  →  pointer_expression > parenthesized_expression > cast_expression
      *(TYPE*) PORT    →  pointer_expression > cast_expression
    """
    if node is None or node.type != "pointer_expression":
        return None

    inner = node.named_children[0] if node.named_children else None
    if inner is None:
        return None

    if inner.type == "parenthesized_expression":
        inner = next(
            (c for c in inner.named_children if c.type == "cast_expression"),
            None,
        )

    if inner is None or inner.type != "cast_expression":
        return None

    val = inner.child_by_field_name("value")
    return val if (val and val.type == "identifier") else None


def _cast_base_type(cast_node) -> str:
    """
    Extract the normalized base type name from a ``cast_expression``'s type field.

    Strips ``volatile``, pointer ``*``, and the leading ``v`` from SGDK's
    volatile-qualified typedefs (``vu16`` → ``u16``, ``vu32`` → ``u32``),
    so that ``u16``, ``vu16``, and ``volatile u16`` all normalize to ``u16``.
    """
    type_node = cast_node.child_by_field_name("type")
    if type_node is None:
        return ""
    raw = type_node.text.decode("utf-8", errors="replace")
    raw = re.sub(r"\bvolatile\b", "", raw).replace("*", "").strip()
    raw = re.sub(r"\bv([us]\d+)\b", r"\1", raw).strip()
    return raw


def _find_enclosing_compound(node):
    """Walk up via ``.parent`` to the nearest ``compound_statement`` ancestor."""
    n = node.parent
    while n is not None:
        if n.type == "compound_statement":
            return n
        n = n.parent
    return None


def _direct_child_at_offset(block, byte_offset: int):
    """Return the direct child of *block* whose byte range contains *byte_offset*."""
    for child in block.children:
        if child.start_byte <= byte_offset < child.end_byte:
            return child
    return None


def _collect_asm_func_removals(root) -> list[dict]:
    """
    Walk the AST from *root* and return removal specs for every
    ``function_definition`` that contains at least one inline-assembly node
    (any node whose type contains the substring ``"asm"``).

    Each spec has the same shape as port-ref specs so that both can be fed
    into a single ``_apply_rewrites`` call.
    """
    results: list[dict] = []

    def _has_asm(node) -> bool:
        if "asm" in node.type:
            return True
        return any(_has_asm(c) for c in node.children)

    def _func_name(node) -> str:
        """Extract the plain identifier name from a function_definition."""
        decl = node.child_by_field_name("declarator")
        while decl is not None:
            if decl.type == "identifier":
                return decl.text.decode("utf-8", errors="replace")
            decl = decl.child_by_field_name("declarator")
        return "?"

    def walk(node) -> None:
        if node.type == "function_definition":
            if _has_asm(node):
                name = _func_name(node)
                print(f"[sgdk_translator] removing asm function '{name}' "
                      f"at line {node.start_point[0] + 1}")
                results.append({
                    "kind":        "asm_func_removal",
                    "node":        node,
                    "replacement": "",
                })
            return  # no nested function_definitions in C
        for child in node.children:
            walk(child)

    walk(root)
    return results


def _collect_port_refs(root) -> list[dict]:
    """
    Walk the AST from *root* and collect rewrite specs.

    Each spec is a dict with:
      kind        — 'write' | 'read'
      macro       — port macro name string
      rule        — PORT_RULES entry
      node        — the Node whose source range will be replaced
      replacement — replacement text (str)
    """
    results: list[dict] = []
    done: set[int] = set()          # node ids already scheduled for rewrite

    def walk(node) -> None:
        if node.id in done:
            return

        if node.type == "assignment_expression":
            left  = node.child_by_field_name("left")
            right = node.child_by_field_name("right")

            # Pattern A — write:  *(cast(PORT)) = rhs
            port_id = _deref_port_id(left)
            if port_id:
                macro = port_id.text.decode("utf-8", errors="replace")
                if macro in _MACRO_TO_RULE:
                    rule = _MACRO_TO_RULE[macro]
                    fn   = rule.get("write_fn")
                    if fn and right:
                        rhs = right.text.decode("utf-8", errors="replace")
                        results.append({
                            "kind": "write", "macro": macro, "rule": rule,
                            "node": node,
                            "replacement": f"{fn}({rhs})",
                        })
                        done.add(node.id)
                        return
                    else:
                        print(f"[sgdk_translator] WARNING: write to read-only port "
                              f"{macro} at line {node.start_point[0] + 1}")
                        done.add(node.id)
                        return

            # Pattern B — read-assign:  lhs = *(cast(PORT))
            port_id = _deref_port_id(right)
            if port_id:
                macro = port_id.text.decode("utf-8", errors="replace")
                if macro in _MACRO_TO_RULE:
                    rule = _MACRO_TO_RULE[macro]
                    fn   = rule.get("read_fn")
                    if fn:
                        results.append({
                            "kind": "read", "macro": macro, "rule": rule,
                            "node": right,          # replace only the RHS deref
                            "replacement": f"{fn}()",
                        })
                        done.add(right.id)
                        if left:
                            walk(left)
                        return


        # Standalone deref read (not the LHS of an assignment)
        if node.type == "pointer_expression" and node.id not in done:
            port_id = _deref_port_id(node)
            if port_id:
                macro = port_id.text.decode("utf-8", errors="replace")
                if macro in _MACRO_TO_RULE:
                    rule = _MACRO_TO_RULE[macro]
                    fn   = rule.get("read_fn")
                    if fn:
                        results.append({
                            "kind": "read", "macro": macro, "rule": rule,
                            "node": node,
                            "replacement": f"{fn}()",
                        })
                        done.add(node.id)
                        return

        for child in node.children:
            if child.id not in done:
                walk(child)

    walk(root)
    return results


def _scan_stmt_for_ptr(var_name: str, stmt_root, rule) -> "tuple[bool, list[dict]]":
    """
    Scan a single statement subtree for uses of *var_name* as a port pointer.

    Returns ``(was_modified, rewrites)``:

    - *was_modified* — True if *var_name* itself (not via deref) is assigned,
      incremented, or appears in any non-deref context (conservative stop).
    - *rewrites*     — rewrite specs for every ``*var_name`` deref found.
    """
    rewrites: list[dict] = []
    done: set[int] = set()
    modified = [False]

    def is_var_deref(n) -> bool:
        if n.type != "pointer_expression":
            return False
        inner = n.named_children[0] if n.named_children else None
        return (inner is not None and inner.type == "identifier" and
                inner.text.decode("utf-8", errors="replace") == var_name)

    def is_var_id(n) -> bool:
        return (n.type == "identifier" and
                n.text.decode("utf-8", errors="replace") == var_name)

    def walk(n) -> None:
        if modified[0] or n.id in done:
            return

        if n.type == "assignment_expression":
            lhs = n.child_by_field_name("left")
            rhs = n.child_by_field_name("right")
            if lhs and is_var_deref(lhs):
                # *var = rhs  →  write
                fn = rule.get("write_fn")
                if fn and rhs:
                    rewrites.append({
                        "kind": "write", "macro": None, "rule": rule,
                        "node": n,
                        "replacement": f"{fn}({rhs.text.decode('utf-8', errors='replace')})",
                    })
                done.add(n.id)
                return
            if lhs and is_var_id(lhs):
                # var = something  →  pointer itself re-assigned
                modified[0] = True
                return

        # var++ / var-- / ++var / --var
        if n.type == "update_expression":
            inner = n.named_children[0] if n.named_children else None
            if inner and is_var_id(inner):
                modified[0] = True
                return

        # *var in read position
        if n.type == "pointer_expression" and is_var_deref(n):
            fn = rule.get("read_fn")
            if fn:
                rewrites.append({
                    "kind": "read", "macro": None, "rule": rule,
                    "node": n,
                    "replacement": f"{fn}()",
                })
            done.add(n.id)
            return

        # bare var in any other context (arg, comparison, …) — conservative stop
        if is_var_id(n):
            modified[0] = True
            return

        for c in n.children:
            walk(c)

    walk(stmt_root)
    return modified[0], rewrites


def _collect_ptr_assign_rewrites(root) -> list[dict]:
    """
    Pattern C dataflow analysis:  ``pw = (TYPE*) PORT`` followed by uses of ``*pw``.

    For each ptr-assign whose cast type matches the rule's ``ptr_type``, scans
    subsequent statements in the same ``compound_statement``:

    - If *pw* is never directly modified (re-assigned, incremented, or used
      bare), rewrites all ``*pw`` derefs and nullifies the ptr-assign itself
      (``pw = NULL`` / ``vu16 *pw = NULL``).
    - Otherwise warns and leaves the code unchanged.

    Handles both forms:
      ``pw = (vu16*) PORT;``          — assignment_expression
      ``vu16 *pw = (vu16*) PORT;``    — init_declarator
    """
    results: list[dict] = []

    def _extract_decl_name(decl) -> "str | None":
        if decl is None:
            return None
        if decl.type == "identifier":
            return decl.text.decode("utf-8", errors="replace")
        return _extract_decl_name(decl.child_by_field_name("declarator"))

    def _ptr_assign_info(node) -> "tuple | None":
        """Return (var_name, cast_node, nullify_node, macro, rule) or None."""
        if node.type == "assignment_expression":
            lhs = node.child_by_field_name("left")
            rhs = node.child_by_field_name("right")
            if not (lhs and rhs and lhs.type == "identifier" and
                    rhs.type == "cast_expression"):
                return None
            cast_node    = rhs
            var_name     = lhs.text.decode("utf-8", errors="replace")
            nullify_node = rhs          # replace only the cast expression
        elif node.type == "init_declarator":
            val = node.child_by_field_name("value")
            if not (val and val.type == "cast_expression"):
                return None
            cast_node    = val
            var_name     = _extract_decl_name(node.child_by_field_name("declarator"))
            if var_name is None:
                return None
            nullify_node = val          # replace only the initializer value
        else:
            return None

        port_id = cast_node.child_by_field_name("value")
        if not (port_id and port_id.type == "identifier"):
            return None
        macro = port_id.text.decode("utf-8", errors="replace")
        if macro not in _MACRO_TO_RULE:
            return None
        return var_name, cast_node, nullify_node, macro, _MACRO_TO_RULE[macro]

    def walk(node) -> None:
        info = _ptr_assign_info(node)
        if info:
            var_name, cast_node, nullify_node, macro, rule = info
            line = node.start_point[0] + 1

            # Type check
            expected = rule.get("ptr_type")
            if expected:
                actual = _cast_base_type(cast_node)
                if actual != expected:
                    print(f"[sgdk_translator] WARNING: ptr-assign '{var_name} = "
                          f"(TYPE*) {macro}' type mismatch "
                          f"(expected '{expected}', got '{actual}') "
                          f"at line {line} — skipping dataflow")
                    for c in node.children:
                        walk(c)
                    return

            block = _find_enclosing_compound(node)
            if block is None:
                print(f"[sgdk_translator] WARNING: ptr-assign '{var_name} = "
                      f"(TYPE*) {macro}' at line {line} — no enclosing block")
                for c in node.children:
                    walk(c)
                return

            stmt = _direct_child_at_offset(block, node.start_byte)
            if stmt is None:
                for c in node.children:
                    walk(c)
                return

            subsequent = [c for c in block.children
                          if c.start_byte > stmt.start_byte]

            all_rewrites: list[dict] = []
            ok = True
            for s in subsequent:
                was_modified, rw = _scan_stmt_for_ptr(var_name, s, rule)
                all_rewrites.extend(rw)
                if was_modified:
                    ok = False
                    break

            if ok:
                print(f"[sgdk_translator] ptr-assign '{var_name} = (TYPE*) {macro}' "
                      f"at line {line}: {len(all_rewrites)} deref rewrite(s)")
                results.append({
                    "kind": "ptr_nullify", "macro": macro, "rule": rule,
                    "node": nullify_node,
                    "replacement": "NULL",
                })
                results.extend(all_rewrites)
            else:
                print(f"[sgdk_translator] WARNING: ptr-assign '{var_name} = "
                      f"(TYPE*) {macro}' at line {line} "
                      f"— pointer modified in scope, manual rewrite needed")
            return

        for c in node.children:
            walk(c)

    walk(root)
    return results


def _apply_rewrites(source_bytes: bytes, refs: list[dict]) -> bytes:
    """Replace each node's byte range with its replacement text.

    Applied in reverse start-offset order so earlier positions stay valid.
    """
    buf = bytearray(source_bytes)
    for ref in sorted(refs, key=lambda r: r["node"].start_byte, reverse=True):
        node = ref["node"]
        buf[node.start_byte : node.end_byte] = ref["replacement"].encode("utf-8")
    return bytes(buf)


def _transform_source_ts(source: str, src_name: str) -> str:
    """
    Tree-sitter pass: remove asm-containing functions and rewrite hardware
    port dereferences.  Returns the (possibly rewritten) source text.

    Falls back to a no-op with a warning if tree-sitter is not installed.
    """
    if not _ts_available():
        print(f"[sgdk_translator] WARNING: tree-sitter not available; "
              f"{src_name} NOT rewritten")
        return source

    parser, _ = _ts_get_parser()
    src_bytes = source.encode("utf-8")
    tree      = parser.parse(src_bytes)
    root      = tree.root_node

    # Pass A — remove functions that contain inline assembly
    asm_removals = _collect_asm_func_removals(root)

    # Pass B — rewrite port dereferences, but skip any that fall inside a
    # function already scheduled for removal (overlapping ranges break the
    # reverse-offset apply algorithm).
    removed_ranges = [(r["node"].start_byte, r["node"].end_byte)
                      for r in asm_removals]

    def _inside_removed(node) -> bool:
        sb, eb = node.start_byte, node.end_byte
        return any(rs <= sb and eb <= re for rs, re in removed_ranges)

    port_refs = [r for r in _collect_port_refs(root)
                 if not _inside_removed(r["node"])]

    # Pass C — ptr-assign dataflow: pw = (TYPE*) PORT; *pw = val;
    ptr_rewrites = [r for r in _collect_ptr_assign_rewrites(root)
                    if not _inside_removed(r["node"])]

    if port_refs:
        print(f"[sgdk_translator] {src_name}: {len(port_refs)} port rewrite(s)")
        for r in port_refs:
            fn_w = r["rule"].get("write_fn", "-")
            fn_r = r["rule"].get("read_fn",  "-")
            print(f"  line {r['node'].start_point[0] + 1:4d}"
                  f"  [{r['kind']:5s}]  {r['macro']}"
                  f"  (w={fn_w} r={fn_r})")

    return _apply_rewrites(src_bytes, asm_removals + port_refs + ptr_rewrites).decode("utf-8", errors="replace")


# ---------------------------------------------------------------------------
# TRANSFORMS helpers
# ---------------------------------------------------------------------------

def _find_func_decl_start(text: str, name_pos: int) -> int:
    """
    Walk backwards from *name_pos* (start of function name) to find the
    first character of the full function declaration (i.e. the return-type
    token, possibly after attributes/macros).

    Stops at the character after the first ';' or '}' found outside any
    brace pair, or at position 0.  Leading whitespace is then skipped
    forward so the returned position lands on the first non-blank character
    of the declaration.
    """
    i = name_pos - 1
    # skip horizontal whitespace immediately before the name
    while i >= 0 and text[i] in " \t":
        i -= 1

    brace_depth = 0
    while i >= 0:
        c = text[i]
        if c == "}":
            if brace_depth == 0:
                i += 1          # stop just after a sibling function's closing brace
                break
            brace_depth += 1
        elif c == "{":
            if brace_depth > 0:
                brace_depth -= 1
            else:
                i += 1          # stop just after the enclosing opening brace
                break
        elif brace_depth == 0 and c == ";":
            i += 1              # stop just after the semicolon
            break
        i -= 1
    else:
        i = 0

    # skip any blank lines / whitespace after the boundary
    while i < name_pos and text[i] in " \t\r\n":
        i += 1
    return i


def _remove_one_func_def(text: str, name: str) -> str:
    """Remove all *definitions* (with a brace body) of C function *name*."""
    pat = re.compile(rf"\b{re.escape(name)}\s*\(")
    pos = 0
    chunks: list[str] = []

    while pos < len(text):
        m = pat.search(text, pos)
        if not m:
            chunks.append(text[pos:])
            break

        # Walk forward through the parameter list to find the closing ')'
        i = m.end()          # just past the opening '('
        depth = 1
        while i < len(text) and depth > 0:
            if text[i] == "(":   depth += 1
            elif text[i] == ")": depth -= 1
            i += 1
        # i is now just past the closing ')'

        # Skip whitespace; is the next non-blank character a '{'?
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1

        if j >= len(text) or text[j] != "{":
            # Call site or prototype — not a definition; advance past match
            chunks.append(text[pos:m.end()])
            pos = m.end()
            continue

        # Find where the full declaration begins
        decl_start = _find_func_decl_start(text, m.start())

        # Match braces to find the end of the function body
        k = j + 1
        depth = 1
        while k < len(text) and depth > 0:
            if text[k] == "{":   depth += 1
            elif text[k] == "}": depth -= 1
            k += 1
        # k is just past the closing '}'
        if k < len(text) and text[k] == "\n":
            k += 1              # consume the trailing newline

        chunks.append(text[pos:decl_start])
        pos = k

    return "".join(chunks)


def _remove_func_defs(text: str, ids: list[str]) -> str:
    for name in ids:
        text = _remove_one_func_def(text, name)
    return text


def _remove_one_func_decl(text: str, name: str) -> str:
    """Remove all *declarations* (prototype ending in ``;``) of C function *name*."""
    pat = re.compile(rf"\b{re.escape(name)}\s*\(")
    pos = 0
    chunks: list[str] = []

    while pos < len(text):
        m = pat.search(text, pos)
        if not m:
            chunks.append(text[pos:])
            break

        # Walk forward through the parameter list to find the closing ')'
        i = m.end()
        depth = 1
        while i < len(text) and depth > 0:
            if text[i] == "(":   depth += 1
            elif text[i] == ")": depth -= 1
            i += 1
        # i is now just past the closing ')'

        # Skip whitespace; is the next non-blank character a ';'?
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1

        if j >= len(text) or text[j] != ";":
            # Call site or definition — not a declaration; advance past match
            chunks.append(text[pos:m.end()])
            pos = m.end()
            continue

        decl_start = _find_func_decl_start(text, m.start())

        k = j + 1
        if k < len(text) and text[k] == "\n":
            k += 1              # consume trailing newline

        chunks.append(text[pos:decl_start])
        pos = k

    return "".join(chunks)


def _remove_func_decls(text: str, ids: list[str]) -> str:
    for name in ids:
        text = _remove_one_func_decl(text, name)
    return text


def _make_one_func_def_not_static(text: str, name: str) -> str:
    """Remove the ``static`` storage class from the definition of *name*.

    - If the definition is found and is ``static``: removes ``static``.
    - If the definition is found but is NOT ``static``: warns (the transform
      was requested but the function is already non-static).
    - If no definition is found: no change (the caller's no-effect warning fires).
    """
    pat = re.compile(rf"\b{re.escape(name)}\s*\(")
    pos = 0
    chunks: list[str] = []

    while pos < len(text):
        m = pat.search(text, pos)
        if not m:
            chunks.append(text[pos:])
            break

        # Walk forward through the parameter list to find closing ')'
        i = m.end()
        depth = 1
        while i < len(text) and depth > 0:
            if text[i] == "(":   depth += 1
            elif text[i] == ")": depth -= 1
            i += 1

        # Skip whitespace; is the next non-blank character a '{'?
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1

        if j >= len(text) or text[j] != "{":
            # Call site or declaration — not a definition
            chunks.append(text[pos:m.end()])
            pos = m.end()
            continue

        # Definition found — examine the declaration prefix (return type + modifiers)
        decl_start  = _find_func_decl_start(text, m.start())
        decl_prefix = text[decl_start : m.start()]

        static_m = re.search(r"\bstatic\s*", decl_prefix)
        if static_m:
            new_prefix = (decl_prefix[:static_m.start()] +
                          decl_prefix[static_m.end():])
            new_prefix = re.sub(r"  +", " ", new_prefix).lstrip()
            chunks.append(text[pos:decl_start])
            chunks.append(new_prefix)
            chunks.append(text[m.start():m.end()])  # function name + '('
        else:
            lineno = text[:m.start()].count("\n") + 1
            print(f"[sgdk_translator] WARNING: func_def_not_static '{name}' "
                  f"at line {lineno} — function is already not static")
            chunks.append(text[pos:m.end()])

        pos = m.end()

    return "".join(chunks)


def _make_func_defs_not_static(text: str, ids: list[str]) -> str:
    for name in ids:
        text = _make_one_func_def_not_static(text, name)
    return text


# Single-line typedef pattern: typedef <origin> <alias>;
_TYPEDEF_RE = re.compile(r"^\s*typedef\s+.+?[\s*]+(\w+)\s*;\s*$")


def _remove_typedefs(text: str, ids: list[str]) -> str:
    """Remove single-line ``typedef`` declarations whose alias is in *ids*."""
    id_set = set(ids)
    lines = text.splitlines(keepends=True)
    return "".join(
        line for line in lines
        if not ((m := _TYPEDEF_RE.match(line)) and m.group(1) in id_set)
    )


def _change_typedefs(text: str, diff: list) -> str:
    """Replace the origin type in single-line ``typedef`` declarations.

    *diff* is a sequence of ``(alias, new_origin_type)`` pairs.
    For each pair, finds ``typedef <anything> alias;`` and replaces the
    origin with *new_origin_type*.
    """
    for alias, new_origin in diff:
        pat = re.compile(rf"(?m)(^\s*typedef\s+)(.+?)(\s+{re.escape(alias)}\s*;)")
        text = pat.sub(rf"\g<1>{new_origin}\g<3>", text)
    return text


def _apply_transforms(text: str, rel_key: str) -> "tuple[str, Path | None]":
    """
    Apply file-specific TRANSFORMS for *rel_key* (POSIX-style relative path).

    Returns ``(new_text, new_rel)`` where *new_rel* is a Path when a
    ``middle_extension`` transform renames the output file, otherwise None.
    """
    ops = TRANSFORMS.get(rel_key)
    if not ops:
        return text, None

    new_rel: Path | None = None
    for t in ops:
        op = t["do"]
        before = text
        if op == "prepend_includes":
            text = _prepend_includes(text, t["includes"])
        elif op == "rm_defines":
            text = _remove_defines(text, t["ids"])
        elif op == "rm_func_def":
            text = _remove_func_defs(text, t["ids"])
        elif op == "rm_func_decl":
            text = _remove_func_decls(text, t["ids"])
        elif op == "rm_typedefs":
            text = _remove_typedefs(text, t["ids"])
        elif op == "change_typedefs":
            text = _change_typedefs(text, t["diff"])
        elif op == "func_def_not_static":
            text = _make_func_defs_not_static(text, t["ids"])
        elif op == "middle_extension":
            p = Path(rel_key)
            new_rel = p.with_name(f"{p.stem}.{t['ext']}{p.suffix}")

        if op != "middle_extension" and text == before:
            print(f"[sgdk_translator] WARNING: transform '{op}' had no effect in {rel_key}")

    return text, new_rel


# ---------------------------------------------------------------------------
# File processing
# ---------------------------------------------------------------------------

def _write(src_path: Path, out_path: Path, data: "str | bytes") -> None:
    """Write *data* to *out_path* and log ``src -> dst``."""
    print(f"  {src_path}  ->  {out_path}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        out_path.write_text(data, encoding="utf-8")
    else:
        out_path.write_bytes(data)


def process_header(src_path: Path, out_path: Path) -> None:
    """Apply pass 1 (macro guarding) to a header file.

    If the content changes, the output file is automatically renamed with a
    ``.patched`` middle extension (e.g. ``vdp.h`` → ``vdp.patched.h``).
    """
    text = src_path.read_text(encoding="utf-8", errors="replace")
    transformed = _guard_hardware_macros(text)
    if transformed != text:
        out_path = out_path.with_name(f"{out_path.stem}.patched{out_path.suffix}")
    _write(src_path, out_path, transformed)


def process_source(src_path: Path, out_path: Path) -> None:
    """Apply pass 2 (port dereference rewriting) to a C source file."""
    text = src_path.read_text(encoding="utf-8", errors="replace")
    text = _transform_source_ts(text, src_path.name)
    _write(src_path, out_path, text)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Transform SGDK sources for NEWBASE_SGDK builds")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--sgdk-root", metavar="DIR",
                       help="Root of the SGDK tree; process inc/ and src/ recursively")
    group.add_argument("--file", metavar="FILE",
                       help="Process a single file (for debugging)")
    p.add_argument("--out-root", metavar="DIR", required=True,
                   help="Output directory (mirrors the SGDK directory structure)")
    return p.parse_args()


def main() -> None:
    args = _parse_args()
    out_root = Path(args.out_root)

    if args.file:
        src = Path(args.file)
        out = out_root / src.name
        if src.suffix in {".h", ".hpp"}:
            process_header(src, out)
        else:
            process_source(src, out)
        return

    sgdk_root = Path(args.sgdk_root)

    search_roots = [sgdk_root / path for path in SEARCH_ROOTS]
    exclude_dirs = set([sgdk_root /  path for path in EXCLUDE_DIRS])

    _include_exts = {f".{e.lstrip('.')}" for e in INCLUDE_EXTENSIONS}

    all_files = (
        src
        for search_root in search_roots
        if search_root.exists()
        for src in search_root.rglob("*")
        if src.is_file()
        and src.suffix in _include_exts
        and not any(src.is_relative_to(ex) for ex in exclude_dirs)
    )

    for src in all_files:
        rel     = src.relative_to(sgdk_root)
        rel_key = rel.as_posix()

        if src.suffix in {".h", ".hpp"}:
            text = src.read_text(encoding="utf-8", errors="replace")
            guarded = _guard_hardware_macros(text)
            guarded, new_rel = _apply_transforms(guarded, rel_key)
            if new_rel is None and guarded != text:
                new_rel = rel.with_name(f"{rel.stem}.patched{rel.suffix}")
            out = out_root / (new_rel if new_rel else rel)
            _write(src, out, guarded)

        elif src.suffix == ".c":
            text = src.read_text(encoding="utf-8", errors="replace")
            text = _transform_source_ts(text, src.name)
            text, new_rel = _apply_transforms(text, rel_key)
            out = out_root / (new_rel if new_rel else rel)
            _write(src, out, text)


if __name__ == "__main__":
    main()
