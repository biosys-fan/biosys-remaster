#!/bin/bash
# First-run import of the two original CDs: identify, import from every input form, cancel, damaged zip, hostile images.
# Needs build/biosys-import. Takes a few minutes; temporary files go to a fresh folder under $TMPDIR and are removed.
cd "$(dirname "$0")/.." || exit 1
python3 -I tests/test_import.py
rc=$?
if [ "$rc" -eq 0 ]; then echo "IMPORT TEST PASSED"; else echo "IMPORT TEST FAILED"; fi
exit $rc
