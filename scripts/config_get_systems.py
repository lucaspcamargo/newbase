#!/usr/bin/env python

import yaml
import sys

assert(len(sys.argv)==2)

cfgfile = sys.argv[1]
conf = yaml.load(open(sys.argv[1], 'r'), yaml.Loader)

sysnames = []
for sysname, sysconf in conf['systems'].items():
    sysnames.append(sysname)

print(";".join(sysnames), end=None)