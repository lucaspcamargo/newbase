#!/usr/bin/env python

import argparse
import jinja2
import datetime as dt
import sys
import os

assert(__name__ == "__main__")
PROGNAME = "codegen_rtti_entry_points.py"

parser = argparse.ArgumentParser(prog=PROGNAME,
                                 description="Processes a header template to generate rtti initialization code.")
parser.add_argument("template")
parser.add_argument("outfile")
parser.add_argument("identifiers")

ns = parser.parse_args(sys.argv[1:])

inp = None
with open(ns.template, 'r') as f:
    inp = f.read()
assert(inp)

data = {
    'progname': PROGNAME,
    'datetime': dt.datetime.now().isoformat(),
    'outname': os.path.basename(ns.outfile),
    'identifiers': ns.identifiers.rstrip(';').split(';')
}

env = jinja2.Environment()
tmpl = env.from_string(inp)
out = tmpl.render(data)

os.makedirs(os.path.dirname(ns.outfile), exist_ok=True)
with open(ns.outfile, 'w') as f:
    f.write(out)

print(f"[codegen_rtti_entry_points] generated '{ns.outfile}'")