// BioSys 4 terminal panels `termsav termoc termevo termrf termhouse` (the power terminals): test access to the rows of the page that is up.
#pragma once
#include <string>
#include <vector>

namespace biosys::bs4term {

struct Row {
    std::string text;         // the "%.1f" text of the row's power sum as the handler draws it
    double sum = 0;           // +0x1e0 + 0x48 * row
    int left = 0, top = 0, right = 0, bottom = 0;
};
// The rows of the terminal panel loaded last (empty if none was loaded or its Init failed before it set the handler).
std::vector<Row> rows();

} // namespace biosys::bs4term
