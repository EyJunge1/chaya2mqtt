#pragma once

#include <cstdint>

enum class DisplayView : uint8_t {
    Unknown      = 0,
    Heart        = 1,
    SetupQr      = 2,
    ProductTitle = 3,
};

inline bool displayViewIsValid(DisplayView view) {
    return view == DisplayView::Unknown || view == DisplayView::Heart
        || view == DisplayView::SetupQr || view == DisplayView::ProductTitle;
}

inline bool displayViewNeedsRefresh(DisplayView current, DisplayView target) {
    return current == DisplayView::Unknown || current != target;
}

inline bool displayRefreshRequired(DisplayView current, DisplayView target,
                                   bool onlyIfViewChanged) {
    return !onlyIfViewChanged || displayViewNeedsRefresh(current, target);
}
