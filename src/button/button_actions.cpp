#include "button_actions.h"

namespace {
ButtonActionHooks s_hooks{};
} // namespace

void buttonSetActionHooks(const ButtonActionHooks& hooks) {
    s_hooks = hooks;
}

const ButtonActionHooks& buttonActionHooks() {
    return s_hooks;
}
