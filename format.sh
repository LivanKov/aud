#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="$SCRIPT_DIR"
SRC_DIR="$PROJECT_ROOT/gui"
MAIN_DIR="$PROJECT_ROOT"
CLANG_FORMAT_FILE="$PROJECT_ROOT/.clang-format"

if [ ! -f "$CLANG_FORMAT_FILE" ]; then
    echo "Error: .clang-format file not found in the project root."
    exit 1
fi

find "$SRC_DIR" -name '*.cpp' -o -name '*.h' | xargs clang-format --style=file

find "$MAIN_DIR" -name '*.cc' | xargs clang-format --style=file