#!/usr/bin/env python3
"""Regenerate src/cpNNN_tables.{cpp,h} from src/tr_utf8_cpNNN.txt.

    tools/mktables.py --integers /path/to/integers 500 1047

`integers` is the DFA table builder from https://github.com/brazilofmux/utf
(gen/integers.cpp; build it with
`c++ -O2 -o integers integers.cpp smutil.cpp ConvertUTF.cpp`).

Each mapping file has one line per EBCDIC byte, `UUUU;ddd;NAME;`, giving
the Unicode code point in hex, the EBCDIC byte in decimal, and the Unicode
character name.  The builder is run with the EBCDIC substitute byte (63) as
the default for unmapped code points; the EBCDIC-to-UTF-8 lookup tables are
appended by this script.  Output is byte-for-byte what is checked in, so a
regeneration with unchanged inputs is a no-op.
"""
import argparse, os, shutil, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

def build(integers, cp):
    name = f"tr_utf8_cp{cp}.txt"
    mapping = os.path.join(SRC, name)
    with tempfile.TemporaryDirectory() as tmp:
        # The builder prefixes the input name with "utf/" in its header
        # comment, so run it inside a utf/ directory with a bare file name.
        os.mkdir(os.path.join(tmp, "utf"))
        shutil.copy(mapping, os.path.join(tmp, "utf", name))
        body, inc = os.path.join(tmp, "body"), os.path.join(tmp, "inc")
        subprocess.run([integers, "-d", "63", "-o", body, "-i", inc, f"tr_cp{cp}", name],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       cwd=os.path.join(tmp, "utf"))
        dfa_body, dfa_inc = open(body).read(), open(inc).read()

    to_cp = {}
    for line in open(mapping):
        u, b, _ = line.rstrip("\n").split(";", 2)
        to_cp[int(b)] = int(u, 16)
    if sorted(to_cp) != list(range(256)):
        raise SystemExit(f"cp{cp}: mapping is not a bijection over 256 bytes")
    enc = [chr(to_cp[b]).encode("utf-8") for b in range(256)]

    lengths = [str(len(e)) for e in enc]
    rows = [", ".join(lengths[i:i + 8]) for i in range(0, 256, 8)]
    lengths_tbl = (f"const uint8_t cp{cp}_to_utf8_lengths[256] = {{\n"
                   + ",\n".join("    " + r for r in rows) + "\n};\n")
    byte_rows = ["    { " + ", ".join(str(x) for x in list(e) + [0] * (4 - len(e))) + " }"
                 for e in enc]
    bytes_tbl = (f"const uint8_t cp{cp}_to_utf8_bytes[256][4] = {{\n"
                 + ",\n".join(byte_rows) + "\n};\n")

    with open(os.path.join(SRC, f"cp{cp}_tables.cpp"), "w") as o:
        o.write(f'#include <cstdint>\n#include "cp{cp}_tables.h"\n\n')
        o.write(dfa_body)
        o.write(lengths_tbl + "\n" + bytes_tbl)
    with open(os.path.join(SRC, f"cp{cp}_tables.h"), "w") as o:
        o.write(dfa_inc.replace("extern LIBMUX_API const", "extern const"))
        o.write(f"extern const uint8_t cp{cp}_to_utf8_lengths[256];\n")
        o.write(f"extern const uint8_t cp{cp}_to_utf8_bytes[256][4];\n")
    print(f"cp{cp}: wrote src/cp{cp}_tables.cpp and src/cp{cp}_tables.h")

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--integers", required=True, help="path to the libutf integers builder")
    ap.add_argument("codepages", nargs="+", help="code page numbers, e.g. 037 500 1047")
    a = ap.parse_args()
    for cp in a.codepages:
        build(a.integers, cp)
