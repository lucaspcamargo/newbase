#!/bin/bash

set +e

icons_dir="res/_nb_core"

icon_names=(
    icon
    icon_exec
    icon_exec_alt
)

resolutions=(
    192
    144
    96
    72
    48
    32
)

for icon in "${icon_names[@]}"; do
    for res in "${resolutions[@]}"; do
        inkscape "${icons_dir}/${icon}.svg" -o "${icons_dir}/${icon}_${res}.png" -w $res -h $res
    done
done
