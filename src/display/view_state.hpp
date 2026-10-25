#pragma once

#include <cstdint>

enum class DisplayView : uint8_t {
    Unknown = 0,
    Heart = 1,
    SetupQr = 2,
    ProductTitle = 3,
    HeartCrack = 4,
    PowerOff = 5,
};

inline auto displayViewIsValid(DisplayView view) -> bool {
    return view == DisplayView::Unknown || view == DisplayView::Heart || view == DisplayView::SetupQr ||
           view == DisplayView::ProductTitle || view == DisplayView::HeartCrack || view == DisplayView::PowerOff;
}

inline auto displayViewNeedsRefresh(DisplayView current, DisplayView target) -> bool {
    return current == DisplayView::Unknown || current != target;
}

inline auto displayRefreshRequired(DisplayView current, DisplayView target, bool onlyIfViewChanged) -> bool {
    return !onlyIfViewChanged || displayViewNeedsRefresh(current, target);
}

inline auto displayViewIsHeartFamily(DisplayView view) -> bool {
    return view == DisplayView::Heart || view == DisplayView::HeartCrack;
}
