#!/bin/bash
# The panel displays of the machines (build/biosys --game): the GDI text and bars of CBiomeSensor, CLung, COceanProbe,
# CHeater, COxygenStore, CAlgaeScrubber, CTideMachine, CTanksCabinet, the pod console, checked against values computed
# independently from the decompiled formulas; screenshots go to build/test/disp/.
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/disp
exec python3 -I tests/test_disp.py
