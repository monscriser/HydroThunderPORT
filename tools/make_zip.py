"""Zips a folder's contents with standard '/' entry names (PowerShell 5.1's
Compress-Archive writes '\\', which other extractors treat as part of the name).

usage: make_zip.py <folder> <out.zip>
"""
import os
import sys
import zipfile

src, out = sys.argv[1], sys.argv[2]
if os.path.exists(out):
    os.replace(out, out + ".old")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            arc = os.path.relpath(full, src).replace(os.sep, "/")
            z.write(full, arc)
with zipfile.ZipFile(out) as z:
    bad = [n for n in z.namelist() if "\\" in n]
    print(out, "entries:", len(z.namelist()), "with backslash:", len(bad), "test:", z.testzip())
if os.path.exists(out + ".old"):
    os.remove(out + ".old")
