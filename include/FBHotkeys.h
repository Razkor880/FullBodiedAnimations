#pragma once
#include <functional>

namespace FBHotkeys {
    using ReloadFn = std::function<void()>;
    void Install(ReloadFn reload);
}
