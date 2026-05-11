#!/usr/bin/env python3
"""
generate_talkie_vocab.py

Two jobs in one pass over a set of TalkiePCM vocabulary header files:

1.  Generate include/newbase/audio/talkie_pcm_vocab.hpp  (--output, optional)
    The compile-time C++ table used when NEWBASE_TALKIE_PCM is ON.

2.  Generate one .rlpcvocab binary per input header         (--rlpcvocab-dir, optional)
    e.g.  Vocab_US_Large.h  →  US_Large.rlpcvocab
          Vocab_US_Clock.h  →  US_Clock.rlpcvocab
    Each file contains every word found in that header.  All ROM-variant
    bitstreams (sp2 / sp4 / sp5 / …) are stored as separate named variants
    of the same word entry — they are NOT split into separate files.

    Binary format (little-endian):
      magic[4] "RLPV" | version uint16 | word_count uint32
      Per word:   name_len uint16 | name[name_len] | var_count uint8
      Per variant: vname_len uint8 | vname[vname_len] | data_len uint32 | data[data_len]

At least one of --output / --rlpcvocab-dir must be provided.
"""

import argparse
import re
import struct
import sys
from collections import OrderedDict, defaultdict
from pathlib import Path


# ---------------------------------------------------------------------------
# Header parsing
# ---------------------------------------------------------------------------

def _parse_symbol(name):
    """Return (prefix, word) for a TalkiePCM symbol name.

    sp2_ZERO   -> ("sp2",  "ZERO")
    spa_PAUSE1 -> ("spa",  "PAUSE1")
    spPAUSE1   -> ("sp",   "PAUSE1")
    other      -> ("",     other)
    """
    m = re.match(r'^(sp\d+)_(.+)$', name)
    if m:
        return m.group(1), m.group(2)
    m = re.match(r'^(sp[a-z]+)_(.+)$', name)
    if m:
        return m.group(1), m.group(2)
    m = re.match(r'^(sp)([A-Z_].*)$', name)
    if m:
        return m.group(1), m.group(2)
    return '', name


def _c_ident(word):
    return re.sub(r'\W', '_', word)


def parse_header(path):
    """Parse one TalkiePCM vocab header.

    Returns an OrderedDict:
        word_name  ->  list of (variant_prefix, data_bytes_or_None)
    preserving first-appearance order of word names.
    Variants within each word preserve the order they appear in the header.
    """
    text = path.read_text()

    # 1. Collect symbol names in declaration order (handles forward-decls too).
    sym_order = []
    seen_sym = set()
    for m in re.finditer(r'\buint8_t\s+(\w+)\s*\[', text):
        sym = m.group(1)
        if sym not in seen_sym:
            seen_sym.add(sym)
            sym_order.append(sym)

    # 2. Parse array bodies where present.
    #    Pattern: uint8_t NAME[] = { 0x.., ... };
    array_data = {}
    for m in re.finditer(
            r'\buint8_t\s+(\w+)\s*\[\s*\]\s*=\s*\{([^}]+)\}',
            text, re.DOTALL):
        sym = m.group(1)
        hex_vals = re.findall(r'0[xX][0-9a-fA-F]+', m.group(2))
        array_data[sym] = bytes(int(v, 16) for v in hex_vals)

    # 3. Build word -> variants table.
    words = OrderedDict()
    for sym in sym_order:
        prefix, word = _parse_symbol(sym)
        if word not in words:
            words[word] = []
        words[word].append((prefix, array_data.get(sym)))

    return words


# ---------------------------------------------------------------------------
# TMS5220 LPC bitstream length scanner
# Must match audio_producer_lpc::get_bits exactly: rev() then MSB-first.
# ---------------------------------------------------------------------------

_TMS_PERIOD = [
    0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
    0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2d, 0x2f, 0x31,
    0x33, 0x35, 0x36, 0x39, 0x3b, 0x3d, 0x3f, 0x42,
    0x45, 0x47, 0x49, 0x4d, 0x4f, 0x51, 0x55, 0x57,
    0x5c, 0x5f, 0x63, 0x66, 0x6a, 0x6e, 0x73, 0x77,
    0x7b, 0x80, 0x85, 0x8a, 0x8f, 0x95, 0x9a, 0xa0,
]


def _rev8(b):
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4)
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2)
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1)
    return b & 0xFF


def scan_lpc_length(data):
    """Return byte count of a TMS5220 LPC bitstream up to and including
    the stop frame (energy index == 0xF)."""
    byte_pos = 0
    bit_pos  = 0

    def get_bits(n):
        nonlocal byte_pos, bit_pos
        word = _rev8(data[byte_pos]) << 8
        if bit_pos + n > 8:
            word |= _rev8(data[byte_pos + 1])
        word = (word << bit_pos) & 0xFFFF
        val  = (word >> (16 - n)) & ((1 << n) - 1)
        bit_pos += n
        if bit_pos >= 8:
            bit_pos -= 8
            byte_pos += 1
        return val

    while True:
        energy = get_bits(4)
        if energy == 0xF:
            break
        if energy == 0x0:
            continue
        repeat     = get_bits(1)
        period_idx = get_bits(6)
        if not repeat:
            for bits in (5, 5, 4, 4):          # K1–K4
                get_bits(bits)
            if _TMS_PERIOD[period_idx]:
                for bits in (4, 4, 4, 3, 3, 3):  # K5–K10
                    get_bits(bits)

    return byte_pos + (1 if bit_pos > 0 else 0)


# ---------------------------------------------------------------------------
# .rlpcvocab writer
# ---------------------------------------------------------------------------

def write_rlpcvocab(out_path, words):
    """Write one .rlpcvocab file.

    words: OrderedDict  word_name -> list of (variant_name, data_bytes_or_None)
    Words or variants without data are silently skipped.
    """
    # Build final list, dropping variants with no data.
    entries = []
    for wname, variants in words.items():
        vs = [(vname, vdata) for vname, vdata in variants if vdata is not None]
        if vs:
            entries.append((wname, vs))

    with open(out_path, 'wb') as f:
        f.write(b'RLPV')
        f.write(struct.pack('<H', 1))              # version
        f.write(struct.pack('<I', len(entries)))   # word_count

        total_bytes = 0
        for wname, variants in entries:
            name_b = wname.encode()
            f.write(struct.pack('<H', len(name_b)))
            f.write(name_b)
            f.write(struct.pack('B', len(variants)))
            for vname, vdata in variants:
                trimmed = vdata[:scan_lpc_length(vdata)]
                vname_b = vname.encode()
                f.write(struct.pack('B', len(vname_b)))
                f.write(vname_b)
                f.write(struct.pack('<I', len(trimmed)))
                f.write(trimmed)
                total_bytes += len(trimmed)

    print(f'  {out_path.name:<32}  {len(entries):4d} words  {total_bytes:8d} bytes')


# ---------------------------------------------------------------------------
# talkie_pcm_vocab.hpp writer  (unchanged logic from before)
# ---------------------------------------------------------------------------

def write_hpp(out_path, headers, words_per_header):
    """Generate talkie_pcm_vocab.hpp by merging all per-header word tables."""
    word_variants = defaultdict(list)
    for header_name, words in zip(headers, words_per_header):
        for word, variants in words.items():
            for prefix, _ in variants:
                sym   = f'{prefix}_{word}' if prefix else word
                entry = (sym, prefix)
                if entry not in word_variants[word]:
                    word_variants[word].append(entry)

    sorted_words = sorted(word_variants.keys())

    lines = [
        '// AUTO-GENERATED by scripts/generate_talkie_vocab.py — do not edit manually.\n',
        '// Re-run the script (or re-configure with NEWBASE_TALKIE_PCM=ON) to regenerate.\n',
        '#pragma once\n',
        '#ifdef NEWBASE_TALKIE_PCM\n',
        '\n',
        '#include <cstdint>\n',
        '#include <cstddef>\n',
        '\n',
    ]
    for h in headers:
        lines.append(f'#include <{h}>\n')
    lines += [
        '\n',
        'namespace nb {\n',
        '\n',
        'struct talkie_vocab_entry {\n',
        '    const char*            word;          // display name, e.g. "ZERO"\n',
        '    const uint8_t* const*  variants;      // null-terminated array of PCM data pointers\n',
        '    const char* const*     variant_names; // null-terminated, parallel to variants (e.g. "sp2")\n',
        '    size_t                 variant_count;\n',
        '};\n',
        '\n',
        'inline const talkie_vocab_entry* talkie_vocab_all(size_t& out_count)\n',
        '{\n',
    ]

    for word in sorted_words:
        variants  = word_variants[word]
        safe      = _c_ident(word)
        syms_str  = ', '.join(v[0] for v in variants) + ', nullptr'
        names_str = ', '.join(f'"{v[1]}"' for v in variants) + ', nullptr'
        lines.append(f'    static const uint8_t* const tv_{safe}_v[]  = {{ {syms_str} }};\n')
        lines.append(f'    static const char* const    tv_{safe}_vn[] = {{ {names_str} }};\n')

    lines += [
        '\n',
        '    static const talkie_vocab_entry TABLE[] = {\n',
    ]
    for word in sorted_words:
        safe = _c_ident(word)
        lines.append(f'        {{ "{word}", tv_{safe}_v, tv_{safe}_vn, {len(word_variants[word])} }},\n')
    lines += [
        '    };\n',
        '    out_count = sizeof(TABLE) / sizeof(TABLE[0]);\n',
        '    return TABLE;\n',
        '}\n',
        '\n',
        '} // namespace nb\n',
        '#endif // NEWBASE_TALKIE_PCM\n',
    ]

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(''.join(lines))
    print(f'[generate_talkie_vocab] {len(sorted_words)} words → {out_path}')


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--vocab-dir', required=True,
                    help='Path to TalkiePCM/src directory containing Vocab_*.h files')
    ap.add_argument('--output', metavar='HPP',
                    help='Path for generated talkie_pcm_vocab.hpp (omit to skip)')
    ap.add_argument('--rlpcvocab-dir', metavar='DIR',
                    help='Directory for generated .rlpcvocab files (omit to skip)')
    ap.add_argument('--headers', nargs='+',
                    default=['Vocab_Special.h', 'Vocab_US_Large.h'],
                    help='Vocab header filenames to process (relative to --vocab-dir)')
    args = ap.parse_args()

    if not args.output and not args.rlpcvocab_dir:
        ap.error('at least one of --output / --rlpcvocab-dir must be provided')

    vocab_dir        = Path(args.vocab_dir)
    words_per_header = []

    for header_name in args.headers:
        header_path = vocab_dir / header_name
        if not header_path.exists():
            print(f'WARNING: {header_path} not found, skipping', file=sys.stderr)
            words_per_header.append(OrderedDict())
            continue
        words_per_header.append(parse_header(header_path))

    if args.output:
        write_hpp(Path(args.output), args.headers, words_per_header)

    if args.rlpcvocab_dir:
        out_dir = Path(args.rlpcvocab_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        print(f'[generate_talkie_vocab] writing .rlpcvocab → {out_dir}')
        for header_name, words in zip(args.headers, words_per_header):
            if not words:
                continue
            # Vocab_US_Large.h → US_Large.rlpcvocab
            stem     = re.sub(r'^Vocab_', '', Path(header_name).stem)
            out_path = out_dir / f'{stem}.rlpcvocab'
            write_rlpcvocab(out_path, words)


if __name__ == '__main__':
    main()
