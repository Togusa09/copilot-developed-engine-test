#include "Engine/Application.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace engine {
Application::Application() : running_(true), frameCounter_(0) {}

int Application::Run() {
    std::cout << "Engine bootstrap starting...\n";

    while (running_ && frameCounter_ < 3) {
        ++frameCounter_;
        std::cout << "Frame " << frameCounter_ << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "Engine shutdown complete.\n";
    return 0;
}

void Application::RequestExit() noexcept {
    running_ = false;
}

bool Application::IsRunning() const noexcept {
    return running_;
}
}
