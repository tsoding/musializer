#!/bin/sh

set -xe

convert -background None logo.svg -resize 256 logo-256.ico
convert -background None logo.svg -resize 256 logo-256.png
