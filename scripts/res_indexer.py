#!/usr/bin/env python

import argparse
import glob
import yaml
import sys
import stat
import os

assert(__name__ == "__main__")
PROGNAME = "res_indexer.py"

parser = argparse.ArgumentParser(prog=PROGNAME,
                                 description="Processes a directory full of resource files and indexes them, generating or updating `index.yaml` inside of it")
parser.add_argument("resdir")

ns = parser.parse_args(sys.argv[1:])

resdir = ns.resdir
assert(os.path.isdir(resdir))

indexfile = os.path.join(resdir, "index.yaml")
files = []

paths = glob.glob("**", root_dir=resdir, recursive=True)
for relpath in paths:
    if relpath == "index.yaml":
        continue
    path = os.path.join(resdir, relpath)
    st:os.stat_result = os.stat(path)
    if not st:
        continue
    if stat.S_ISDIR(st.st_mode):
        continue
    files.append([relpath, st.st_size,])

print(f"[res_indexer.py] writing resource index ({len(files)} files) to '{indexfile}'")
out = yaml.dump(files)
with open(indexfile, 'w') as f:
    f.write(out)