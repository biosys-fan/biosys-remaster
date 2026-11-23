#!/bin/bash
# Fixes of audit packet b (parity findings outside the features): cross-fade lookup path (dll:10021d60), alarm clock font (dll:10015f20,
# dll:10016230), headless.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/audit_b
exec python3 -I tests/test_audit_b.py
