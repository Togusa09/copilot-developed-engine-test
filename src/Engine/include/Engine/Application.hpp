#pragma once

#include <cstdint>

namespace engine {
class Application {
public:
    Application();
    int Run();
    void RequestExit() noexcept;
    bool IsRunning() const noexcept;

private:
    bool running_;
    std::uint64_t frameCounter_;
};
}
