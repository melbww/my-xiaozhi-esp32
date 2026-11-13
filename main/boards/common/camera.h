#ifndef CAMERA_H
#define CAMERA_H

#include <expected>
#include <string>

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    // True while a camera is present and usable, even if its stream is idle.
    // UVC cameras report this from the driver's connection probe; cameras that
    // cannot tell fall back to IsStreaming(), so their indicator is unchanged.
    // The status bar shows the "camera present" icon for this state.
    virtual bool IsConnected() const { return IsStreaming(); }
    // True while the camera is producing frames: continuously for built-in
    // sensors, and during a capture (plus any idle linger) for on-demand UVC
    // cameras. The status bar shows the "capturing" icon for this state.
    virtual bool IsStreaming() const { return false; }
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::expected<std::string, std::string> Explain(const std::string& question) = 0;
};

#endif  // CAMERA_H
