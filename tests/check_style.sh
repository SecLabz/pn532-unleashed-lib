#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
formatter=${CLANG_FORMAT:-clang-format-17}
version=$("$formatter" --version)
case "$version" in
    *"version 17."*) ;;
    *) echo "clang-format 17 is required" >&2; exit 2 ;;
esac
find src include examples tests -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
    -exec "$formatter" --dry-run --Werror {} +
"${PYTHON:-python3}" -m black --check --line-length 80 tests tools
