"""Extract the SPIR-V words from an SDK shader header (const uint32_t x[] = {...}).
usage: python3 h2spv.py <header.h> <out.spv>"""
import re, struct, sys
text = open(sys.argv[1]).read()
text = text[text.rindex('#endif'):]
body = text[text.index('{') + 1:text.rindex('}')]
words = [int(w, 16) for w in re.findall(r'0x[0-9A-Fa-f]+', body)]
open(sys.argv[2], 'wb').write(struct.pack('<%dI' % len(words), *words))
print(len(words), 'words')
