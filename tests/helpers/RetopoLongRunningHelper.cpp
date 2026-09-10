#include <chrono>
#include <csignal>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

int main()
{
#ifndef _WIN32
    std::signal(SIGTERM, SIG_IGN);
#else
    SetConsoleCtrlHandler(nullptr, TRUE);
#endif
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
