#!/bin/bash
# The original DLL's routines against the port's translations: ported or accounted for, same names used, same routines called.
# Static reading of the decompiler listing; nothing of the original is executed. See tests/audit/README.md.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I tests/test_parity.py
