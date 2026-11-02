#pragma once

#include <cmath>
#include <vector>

namespace NativeDisplay {

struct Mode {
    int width = 0;
    int height = 0;
    double refreshRate = 0;
};

struct Output {
    bool connected;
    bool active;
    Mode mode;
};

inline bool validResolution(const Mode& mode)
{
    return mode.width > 0 && mode.height > 0 &&
            mode.width <= 8192 && mode.height <= 8192;
}

// The streaming protocol accepts integer FPS, not a refresh-rate fraction.
inline int streamFrameRate(double refreshRate)
{
    if (!std::isfinite(refreshRate) || refreshRate < 10 || refreshRate > 9999) {
        return 0;
    }
    return static_cast<int>(std::lround(refreshRate));
}

inline double timingRefreshRate(unsigned int clockKHz, unsigned int htotal,
                                unsigned int vtotal, bool interlaced,
                                bool doubleScan, unsigned int vscan)
{
    if (clockKHz == 0 || htotal == 0 || vtotal == 0) {
        return 0;
    }
    double rate = static_cast<double>(clockKHz) * 1000 / htotal / vtotal;
    if (interlaced) {
        rate *= 2;
    }
    if (doubleScan) {
        rate /= 2;
    }
    if (vscan > 1) {
        rate /= vscan;
    }
    return rate;
}

inline bool selectPhysicalMode(const std::vector<Output>& outputs, Mode& mode)
{
    const Output* selected = nullptr;
    for (const auto& output : outputs) {
        if (!output.connected || !output.active) {
            continue;
        }
        // An unreadable/invalid active output still makes selection ambiguous.
        if (selected != nullptr) {
            return false;
        }
        selected = &output;
    }
    if (selected == nullptr || !validResolution(selected->mode)) {
        return false;
    }
    mode = selected->mode;
    return true;
}

inline Mode resolve(const Mode& fixed, const Mode& current,
                    bool nativeResolution, bool nativeFrameRate)
{
    Mode result = fixed;
    if (nativeResolution && validResolution(current)) {
        result.width = current.width;
        result.height = current.height;
    }
    if (nativeFrameRate && streamFrameRate(current.refreshRate) != 0) {
        result.refreshRate = streamFrameRate(current.refreshRate);
    }
    return result;
}

}
