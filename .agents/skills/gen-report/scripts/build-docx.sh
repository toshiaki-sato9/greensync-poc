#!/usr/bin/env bash
set -euo pipefail

INPUT="${1:-}"
OUTPUT="${2:-}"

if [[ -z "$INPUT" || -z "$OUTPUT" ]]; then
  echo "Usage: $0 <report.md> <report.docx>" >&2
  exit 1
fi

pandoc "$INPUT"           --toc           --number-sections           --from=gfm           --to=docx           -o "$OUTPUT"
