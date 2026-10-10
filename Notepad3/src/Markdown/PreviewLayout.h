#pragma once
#include <algorithm>

namespace np3 {
inline int PreviewWidth(int available, int ratio) {
    if (available <= 0) return 0;
    if (available < 320) return available / 3;
    return std::clamp(static_cast<int>(static_cast<long long>(available) * ratio / 10000), 160, available - 160);
}
inline int PreviewRatio(int available, int requested) {
    if (available <= 0) return 5000;
    return std::clamp(static_cast<int>(static_cast<long long>(requested) * 10000 / available), 500, 9500);
}
}
