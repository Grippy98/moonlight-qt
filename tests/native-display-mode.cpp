#include "../app/streaming/nativedisplaymode.h"

#include <cassert>
#include <limits>
#include <iostream>

using NativeDisplay::Mode;
using NativeDisplay::Output;

int main()
{
    Mode selected;
    const Mode panel{1920, 1200, 144};
    const Mode television{3840, 2160, 120};
    const Mode fixed{1280, 720, 60};

    // The built-in screen is still connected while its CRTC is disabled.
    assert(NativeDisplay::selectPhysicalMode({{true, false, panel},
                                               {true, true, television}}, selected));
    assert(selected.width == 3840 && selected.height == 2160 && selected.refreshRate == 120);
    // A fresh query after undocking chooses the active panel.
    assert(NativeDisplay::selectPhysicalMode({{true, true, panel},
                                               {false, false, television}}, selected));
    assert(selected.width == 1920 && selected.height == 1200 && selected.refreshRate == 144);

    // Never guess the largest/first output when several physical displays are active.
    assert(!NativeDisplay::selectPhysicalMode({{true, true, panel},
                                                {true, true, television}}, selected));
    assert(!NativeDisplay::selectPhysicalMode({{true, true, television},
                                                {true, true, television}}, selected));
    assert(!NativeDisplay::selectPhysicalMode({}, selected));
    assert(!NativeDisplay::selectPhysicalMode({{true, false, panel}}, selected));
    assert(!NativeDisplay::selectPhysicalMode({{true, true, {0, 2160, 120}}}, selected));
    assert(!NativeDisplay::selectPhysicalMode({{true, true, {16384, 8640, 120}}}, selected));

    auto resolved = NativeDisplay::resolve(fixed, television, true, true);
    assert(resolved.width == 3840 && resolved.height == 2160 && resolved.refreshRate == 120);
    resolved = NativeDisplay::resolve(fixed, panel, true, false);
    assert(resolved.width == 1920 && resolved.height == 1200 && resolved.refreshRate == 60);
    resolved = NativeDisplay::resolve(fixed, panel, false, true);
    assert(resolved.width == 1280 && resolved.height == 720 && resolved.refreshRate == 144);
    resolved = NativeDisplay::resolve(fixed, television, false, false);
    assert(resolved.width == 1280 && resolved.height == 720 && resolved.refreshRate == 60);

    // Missing or invalid discovery never replaces the saved manual fallback.
    resolved = NativeDisplay::resolve(fixed, {0, 0, 0}, true, true);
    assert(resolved.width == 1280 && resolved.height == 720 && resolved.refreshRate == 60);
    resolved = NativeDisplay::resolve(fixed, {1920, 1200, 0}, true, true);
    assert(resolved.width == 1920 && resolved.height == 1200 && resolved.refreshRate == 60);
    resolved = NativeDisplay::resolve(fixed, {0, 0, 144}, true, true);
    assert(resolved.width == 1280 && resolved.height == 720 && resolved.refreshRate == 144);

    // Preserve nominal fractional-rate displays instead of truncating to 59/119 FPS.
    assert(NativeDisplay::streamFrameRate(59.94) == 60);
    assert(NativeDisplay::streamFrameRate(119.88) == 120);
    assert(NativeDisplay::streamFrameRate(143.99) == 144);
    assert(NativeDisplay::streamFrameRate(0) == 0);
    assert(NativeDisplay::streamFrameRate(-144) == 0);
    assert(NativeDisplay::streamFrameRate(10000) == 0);
    assert(NativeDisplay::streamFrameRate(std::numeric_limits<double>::infinity()) == 0);
    assert(NativeDisplay::streamFrameRate(std::numeric_limits<double>::quiet_NaN()) == 0);

    assert(NativeDisplay::timingRefreshRate(148500, 2200, 1125, false, false, 0) == 60);
    assert(NativeDisplay::timingRefreshRate(148500, 2200, 1125, true, false, 0) == 120);
    assert(NativeDisplay::timingRefreshRate(148500, 2200, 1125, false, true, 0) == 30);
    assert(NativeDisplay::timingRefreshRate(148500, 2200, 1125, false, false, 2) == 30);
    assert(NativeDisplay::timingRefreshRate(0, 2200, 1125, false, false, 0) == 0);
    assert(NativeDisplay::timingRefreshRate(148500, 0, 1125, false, false, 0) == 0);
    assert(NativeDisplay::timingRefreshRate(148500, 2200, 0, false, false, 0) == 0);

    std::cout << "Native display selection, fallback and fractional refresh tests passed\n";
}
