#!/bin/sh

set -xe

convert -background None ./resources/logo/logo.svg -resize 256 ./resources/logo/logo-256.ico
convert -background None ./resources/logo/logo.svg -resize 256 ./resources/logo/logo-256.png
