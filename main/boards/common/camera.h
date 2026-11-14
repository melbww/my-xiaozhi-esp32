#ifndef CAMERA_H
#define CAMERA_H

#include <cstddef>
#include <expected>
#include <functional>
#include <string>

class Camera {
public:
    // Receives one JPEG chunk; returning false aborts the transfer.
    using JpegSink = std::function<bool(const void* data, size_t len)>;

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
    // True when WriteJpeg() can produce a local JPEG snapshot. Cameras that can
    // only upload through Explain() return false.
    virtual bool SupportsJpegSnapshot() const { return false; }
    virtual std::expected<std::string, std::string> Explain(const std::string& question) = 0;
    // Stream the frame produced by the last successful Capture() as JPEG to
    // `sink`, without uploading it anywhere. Used by local transports that hand
    // the photo to the user directly (e.g. CameraHttpServer) instead of asking
    // a vision model. Returns the number of JPEG bytes produced.
    virtual std::expected<size_t, std::string> WriteJpeg(const JpegSink& sink, int quality = 80) {
        (void)sink;
        (void)quality;
        return std::unexpected("JPEG snapshot is not supported by this camera");
    }
};

#endif  // CAMERA_H
