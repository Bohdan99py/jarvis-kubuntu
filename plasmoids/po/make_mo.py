#!/usr/bin/env python3
"""Compiles a gettext .po file into a .mo file (no gettext tools needed).

Handles what plasmoid/po uses: msgid/msgstr pairs with escaped quotes, no
plural forms or contexts.  Usage: make_mo.py <in.po> <out.mo>
"""
import ast
import struct
import sys


def parse(path):
    entries, key, current = {}, None, None
    for raw in open(path, encoding='utf-8'):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('msgid '):
            key, current = 'id', [ast.literal_eval(line[6:])]
            msgid = current
        elif line.startswith('msgstr '):
            key, current = 'str', [ast.literal_eval(line[7:])]
            entries[''.join(msgid)] = current
        elif line.startswith('"') and current is not None:
            current.append(ast.literal_eval(line))
    return {k: ''.join(v) for k, v in entries.items() if ''.join(v) or k == ''}


def write_mo(entries, path):
    keys = sorted(entries)
    ids = b''.join(k.encode() + b'\0' for k in keys)
    strs = b''.join(entries[k].encode() + b'\0' for k in keys)
    n = len(keys)
    header = 7 * 4
    ids_table, strs_table = header, header + n * 8
    ids_start = strs_table + n * 8
    strs_start = ids_start + len(ids)
    out = struct.pack('<7I', 0x950412de, 0, n, ids_table, strs_table, 0, 0)
    offset = 0
    for k in keys:
        out += struct.pack('<2I', len(k.encode()), ids_start + offset)
        offset += len(k.encode()) + 1
    offset = 0
    for k in keys:
        out += struct.pack('<2I', len(entries[k].encode()), strs_start + offset)
        offset += len(entries[k].encode()) + 1
    with open(path, 'wb') as f:
        f.write(out + ids + strs)


if __name__ == '__main__':
    write_mo(parse(sys.argv[1]), sys.argv[2])
