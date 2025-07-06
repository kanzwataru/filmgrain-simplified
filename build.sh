#!/bin/sh
mkdir -p build
gcc -Og -g3 src/main.c -lm -lSDL3 -o build/filmgrain-simplified
