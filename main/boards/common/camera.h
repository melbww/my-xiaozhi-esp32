#ifndef CAMERA_H
#define CAMERA_H

#include <expected>
#include <string>

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    // True while the camera is producing frames: continuously for built-in
    // sensors, and during the capture plus a short linger for on-demand UVC
    // cameras. Displays use this for the status-bar camera indicator; cameras
    // that cannot report it stay hidden.
    virtual bool IsStreaming() const { return false; }
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::expected<std::string, std::string> Explain(const std::string& question) = 0;
};

#endif  // CAMERA_H
