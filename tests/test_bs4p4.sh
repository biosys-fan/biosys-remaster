#!/bin/bash
# BioSys 4 device panels A (packet P4, src/engine/bs4panel_{sched,range,fans,gauges}.cpp + bs4panels_a.cpp): pages 54 panelcfan, 55 panelsfan,
# 56 panelheat, 57 panelrain, 59 panelmist, 60 panelvent, 64 panelsump, 65 panelosm, 66 paneluv, 67 panelfilt, 68 paneltide, 69 panelwave,
# opened by real clicks over the real game logic (build/biosys-bs4test --game). Frames: build/shots/bs4p4_*.bmp
cd "$(dirname "$0")/.." || exit 1
mkdir -p build/test/bs4p4 build/shots
exec python3 -I tests/test_bs4p4.py
