#!/bin/bash
# Convert SVG panel files to PNG for MetaModule
# Requires Inkscape v1.2.2+ in PATH
#
# Usage: ./scripts/convert-panels.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
INPUT_DIR="$PROJECT_DIR/assets/panels"
OUTPUT_DIR="$PROJECT_DIR/assets/panels"

# MetaModule faceplate height is 240px
HEIGHT=240

echo "Converting SVG panels to PNG (height=${HEIGHT}px)..."

for svg in "$INPUT_DIR"/*.svg; do
    [ -f "$svg" ] || continue
    basename=$(basename "$svg" .svg)
    png="$OUTPUT_DIR/${basename}.png"
    echo "  $basename.svg -> $basename.png"
    inkscape "$svg" --export-type=png --export-filename="$png" --export-height=$HEIGHT
done

echo "Done. PNGs are in $OUTPUT_DIR/"
echo ""
echo "Note: After conversion, you can remove the SVG files from assets/panels/"
echo "      The .mmplugin only needs the PNGs."
