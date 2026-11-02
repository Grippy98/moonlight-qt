#define SDL_MAIN_HANDLED
#include "streaming/streamutils.h"

#include <QGuiApplication>
#include <QScreen>
#include <QAtomicInt>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

#ifdef MOONLIGHT_NATIVE_PROBE_GLOBAL_STUB
// The application normally supplies this logging counter in main.cpp.
QAtomicInt g_AsyncLoggingEnabled{0};
#endif

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    const bool watch = argc == 3 && std::strcmp(argv[1], "--watch") == 0;
    const int seconds = watch ? std::atoi(argv[2]) : 0;
    if (watch && (seconds < 1 || seconds > 120)) {
        std::fprintf(stderr, "Watch duration must be 1–120 seconds.\n");
        SDL_Quit();
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    NativeDisplay::Mode mode;
    int fps;
    do {
        mode = StreamUtils::getCurrentNativeMode(0, app.primaryScreen());
        fps = NativeDisplay::streamFrameRate(mode.refreshRate);
        std::printf("{\"width\":%d,\"height\":%d,\"refresh\":%.6f,\"fps\":%d}\n",
                    mode.width, mode.height, mode.refreshRate, fps);
        std::fflush(stdout);
        if (!watch) break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    SDL_Quit();
    if (argc == 4 && (mode.width != std::atoi(argv[1]) ||
                      mode.height != std::atoi(argv[2]) || fps != std::atoi(argv[3]))) {
        std::fprintf(stderr, "Active display differs from the expected mode.\n");
        return 2;
    }
    return NativeDisplay::validResolution(mode) && fps > 0 ? 0 : 1;
}
