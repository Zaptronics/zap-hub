# tools/xxd_like.py
import sys, pathlib

# Usage: python xxd_like.py input_file output_c symbol_name
inp = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
sym = sys.argv[3]

data = inp.read_bytes()

# make a nice C file
hex_bytes = ", ".join(f"0x{b:02x}" for b in data)
out.write_text(
    f'#include <stddef.h>\n'
    f'const unsigned char {sym}[] = {{ {hex_bytes} }};\n'
    f'const size_t {sym}_len = {len(data)};\n'
)
