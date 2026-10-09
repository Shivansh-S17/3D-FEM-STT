#!/bin/bash

# Arrays
perl -pe 's/KK_red\((\d+),(\d+)\)/"KK_red[" . ($1-1) . "," . ($2-1) . "]"/eg' KK_red.txt > KK_red_python.txt
perl -pe 's/KK_full\((\d+),(\d+)\)/"KK_full[" . ($1-1) . "," . ($2-1) . "]"/eg' KK_full.txt > KK_full_python.txt
perl -pe 's/Ks_red\((\d+),(\d+)\)/"Ks_red[" . ($1-1) . "," . ($2-1) . "]"/eg' Ks_red.txt > Ks_red_python.txt
perl -pe 's/Ks_full\((\d+),(\d+)\)/"Ks_full[" . ($1-1) . "," . ($2-1) . "]"/eg' Ks_full.txt > Ks_full_python.txt
perl -pe 's/D\((\d+),(\d+)\)/"DD[" . ($1-1) . "," . ($2-1) . "]"/eg' DD.txt > DD_python.txt
perl -pe 's/B\((\d+),(\d+)\)/"BB[" . ($1-1) . "," . ($2-1) . "]"/eg' Bb.txt > Bb_python.txt

# Vectors
perl -pe 's/\bFF1\s*\(\s*(\d+)\s*,\s*1\s*\)/"FF[" . ($1 - 1) . "]"/eg' FF1.txt > FF1_python.txt


