// Development only. BIOSYS_DEV_ROOT is the developer's folder above the repository (the original discs in setup-cd/ and
// game-cd/, the analysis notes), handed in by the Makefile (DEVFLAGS). Test tools include this header to have a default;
// the app does not: built without the macro (the release), it has no development fallback at all.
#pragma once
#ifndef BIOSYS_DEV_ROOT
#define BIOSYS_DEV_ROOT ".."
#endif
