#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
PYTHONPATH="$PWD${PYTHONPATH:+:$PYTHONPATH}" \
    "${PYTHON:-python3}" -m unittest discover -s tests -t . -p 'test_*.py' -v
