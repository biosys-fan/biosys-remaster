#!/bin/bash
# EngineApi / GameLogic interface: command table, DLL wrapper table, API vs direct subsystem calls,
# restriction masks, event order. (The full suite itself is run by tests/run.sh.)
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/api
exec python3 -I tests/test_api.py
