#!/bin/sh


set +e

inkscape res/_nb_core/icon.svg -o res/_nb_core/icon_192.png -w 192 -h 192
inkscape res/_nb_core/icon.svg -o res/_nb_core/icon_144.png -w 144 -h 144
inkscape res/_nb_core/icon.svg -o res/_nb_core/icon_96.png -w 96 -h 96
inkscape res/_nb_core/icon.svg -o res/_nb_core/icon_72.png -w 72 -h 72
inkscape res/_nb_core/icon.svg -o res/_nb_core/icon_48.png -w 48 -h 48

