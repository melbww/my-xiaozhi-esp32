#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "esp_video_init.h"
#include "jpg/image_to_jpeg.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class EspVideo : public Camera {
private:
    struct FrameBuffer {
        uint8_t* data = nullptr;
        size_t len = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    int video_fd_ = -1;
    const char* video_device_name_ = nullptr;
    std::atomic<bool> streaming_on_{false};
    bool is_uvc_ = false;
    bool is_mipi_csi_ = false;
    struct MmapBuffer {
        void* start = nullptr;
        size_t length = 0;
    };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;

    // A streaming UVC module is a significant power load, so the stream is
    // opened on demand for a photo and closed again after a short idle linger.
    // stream_wanted_ is true while a capture (or its linger) still needs the
    // stream; stream_idle_deadline_ is the tick at which the monitor stops it.
    std::atomic<bool> stream_wanted_{false};
    std::atomic<TickType_t> stream_idle_deadline_{0};

    // The UVC stream stays "started" after the camera is unplugged, so probe
    // VIDIOC_G_PARM (which checks the driver's connected dev_addr) instead of
    // trusting STREAMON. A reconnect also runs STREAMOFF to reset the esp_video
    // state, because the UVC host already closed the old stream.
    std::thread monitor_thread_;
    std::atomic<bool> monitor_stop_{false};
    std::atomic<bool> camera_connected_{false};
    std::atomic<bool> camera_unplugged_{false};
    std::mutex device_mutex_;
    int recovery_attempts_ = 0;
    bool log_levels_saved_ = false;
    esp_log_level_t usb_log_level_ = ESP_LOG_INFO;
    esp_log_level_t video_log_level_ = ESP_LOG_INFO;

    bool SetupDevice();
    // Retries open() when the camera was absent at boot; open() re-runs the
    // device init, so a camera plugged in later is picked up without a reboot.
    bool ReopenDevice();
    // Both require device_mutex_ to be held by the caller.
    bool StartStreamLocked();
    void StopStreamLocked();
    void RefreshCameraIndicator();
    bool RecoverCamera();
    void SuppressDisconnectLogs();
    void RestoreDisconnectLogs();
    void StartStreamMonitor();
    void MonitorStream();

public:
    EspVideo(const esp_video_init_config_t& config);
    ~EspVideo() override;

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool IsConnected() const override;
    virtual bool IsStreaming() const override;
    virtual bool Capture();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual bool SupportsJpegSnapshot() const override { return true; }
    virtual std::expected<std::string, std::string> Explain(const std::string& question) override;
    virtual std::expected<size_t, std::string> WriteJpeg(const JpegSink& sink,
                                                         int quality = 80) override;
};
