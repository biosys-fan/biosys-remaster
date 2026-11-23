#!/bin/bash
# Every method the original game DLL defines in a class's virtual table is mentioned (by its address) somewhere in the port's sources,
# or is on the short list of methods that only write the debug log. Static reading of the DLL file; nothing of it is executed.
cd "$(dirname "$0")/.." || exit 1
exec python3 -I tests/test_vtables.py
