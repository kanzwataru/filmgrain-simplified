#!/bin/sh
mkdir -p build
gcc -O2 src/main.c -lm -lSDL3 -o build/filmgrain-simplified
