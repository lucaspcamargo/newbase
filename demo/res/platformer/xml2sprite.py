#!/usr/bin/env python3

# xml2sprite.py -- a helper script to convert Kenney's sprotesheet format to our .sprite definition

from lxml import etree
import sys
import yaml

infile = sys.argv[1]
outfile = infile.replace(".xml", ".sprite")
texture_path = infile.replace(".xml", ".png")
subtextures = []

tree:etree.ElementTree = etree.parse(infile)
root:etree.Element = tree.getroot()
for subtex in root:
    subtex:etree.Element
    name = subtex.get("name").replace(".png", "")
    x = subtex.get("x")
    y = subtex.get("y")
    w = subtex.get("width")
    h = subtex.get("height")
    subtextures.append({
        'name': name,
        'x': x,
        'y': y,
        'w': w,
        'h': h
    })

print(f"Found {len(subtextures)} subtextures in input file")

seqs = []

for sub in subtextures:
    seqs.append({
        "name": sub["name"],
        "loop": True,
        "frames": [{"source_rect":[sub["x"], sub["y"], sub["w"], sub["h"]], "duration": 1.0},]
    })

out_data = {
    "texture": texture_path,
    "sequences": seqs
}

with open(outfile, "w") as out_f:
    out_f.write(yaml.dump(out_data))

print(f"Wrote to '{outfile}'")
