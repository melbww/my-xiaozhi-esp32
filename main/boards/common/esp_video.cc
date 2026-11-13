#include "sdkconfig.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include "esp_imgfx_color_convert.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"

#include "board.h"
#include "display.h"
#include "esp_jpeg_common.h"
#include "esp_video.h"
#include "jpg/image_to_jpeg.h"
#include "jpg/jpeg_to_image.h"
#include "lvgl_display.h"
#include "mcp_server.h"
#include "system_info.h"

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL MAX(CONFIG_LOG_DEFAULT_LEVEL, ESP_LOG_DEBUG)
#endif                // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#include <esp_log.h>  // should be after LOCAL_LOG_LEVEL definition

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "driver/ppa.h"
#if defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_90)
#define IMAGE_ROTATION_ANGLE (PPA_SRM_ROTATION_ANGLE_270)
#elif defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_270)
#define IMAGE_ROTATION_ANGLE (PPA_SRM_ROTATION_ANGLE_90)
#else
#error "CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE is not set"
#endif  // angle
#else   // target
#include "esp_imgfx_rotate.h"
#if defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_90)
#define IMAGE_ROTATION_ANGLE (90)
#elif defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_270)
#define IMAGE_ROTATION_ANGLE (270)
#else
#error "CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE is not set"
#endif  // angle
#endif  // target
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE

#define TAG "EspVideo"

// A UVC module is powered and streaming only while a photo is being taken, plus
// a short linger so a follow-up question reuses the open stream.
static constexpr uint32_t kStreamIdleTimeoutMs = CONFIG_XIAOZHI_CAMERA_UVC_STREAM_IDLE_SEC * 1000;
// A freshly opened UVC stream needs about a second for auto-exposure to settle;
// frames captured earlier than this come out noticeably brighter.
static constexpr uint32_t kStreamStartSettleMs = 1000;

#if CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED
#if CONFIG_XIAOZHI_CAMERA_HMIRROR
static constexpr bool kConfiguredHMirror = true;
#else
static constexpr bool kConfiguredHMirror = false;
#endif
#if CONFIG_XIAOZHI_CAMERA_VFLIP
static constexpr bool kConfiguredVFlip = true;
#else
static constexpr bool kConfiguredVFlip = false;
#endif
#endif

#if defined(CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER) || \
    defined(CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP)
#pragma message( \
    "CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER or CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP is enabled; verify YUV422 image integrity")
#endif

#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#define CAM_PRINT_FOURCC(pixelformat)       \
    char fourcc[5];                         \
    fourcc[0] = pixelformat & 0xFF;         \
    fourcc[1] = (pixelformat >> 8) & 0xFF;  \
    fourcc[2] = (pixelformat >> 16) & 0xFF; \
    fourcc[3] = (pixelformat >> 24) & 0xFF; \
    fourcc[4] = '\0';                       \
    ESP_LOGD(TAG, "FOURCC: '%c%c%c%c'", fourcc[0], fourcc[1], fourcc[2], fourcc[3]);

// for compatibility with old esp_video version
#ifndef MAP_FAILED
#define MAP_FAILED nullptr
#endif

__attribute__((weak)) esp_err_t esp_video_deinit(void) { return ESP_ERR_NOT_SUPPORTED; }
// end of for compatibility with old esp_video version

static void log_available_video_devices() {
    for (int i = 0; i < 50; i++) {
        char path[16];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            ESP_LOGD(TAG, "found video device: %s", path);
            close(fd);
        }
    }
}
#else
#define CAM_PRINT_FOURCC(pixelformat) (void)0;
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

EspVideo::EspVideo(const esp_video_init_config_t& config) {
    is_uvc_ = config.usb_uvc != nullptr;

    if (esp_video_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed");
        return;
    }

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

    const char* video_device_name = nullptr;

    if (false) { /* 用于构建 else if */
    }
#if CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
    else if (config.csi != nullptr) {
        video_device_name = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE
    else if (config.dvp != nullptr) {
        video_device_name = ESP_VIDEO_DVP_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE
    else if (config.jpeg != nullptr) {
        video_device_name = ESP_VIDEO_JPEG_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE
    else if (config.spi != nullptr) {
        video_device_name = ESP_VIDEO_SPI_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
    else if (config.usb_uvc != nullptr) {
        video_device_name = ESP_VIDEO_USB_UVC_DEVICE_NAME(0);
    }
#endif

    if (video_device_name == nullptr) {
        ESP_LOGE(TAG, "no video device is enabled");
        return;
    }

    video_fd_ = open(video_device_name, O_RDWR);
    video_device_name_ = video_device_name;

    if (video_fd_ < 0) {
        ESP_LOGE(TAG, "open %s failed, errno=%d(%s)", video_device_name, errno, strerror(errno));
#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
        log_available_video_devices();
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
        if (is_uvc_) {
            // open() runs the UVC init, which waits for the camera. Keep the
            // monitor running so a camera plugged in later is picked up.
            ESP_LOGW(TAG, "Camera not attached yet, will keep probing");
            SuppressDisconnectLogs();
            StartStreamMonitor();
        }
        return;
    }

    is_mipi_csi_ = strcmp(video_device_name, ESP_VIDEO_MIPI_CSI_DEVICE_NAME) == 0;
    SetupDevice();
}

bool EspVideo::SetupDevice() {
    if (is_uvc_) {
        // Bound DQBUF so a photo capture fails instead of blocking forever when
        // the camera has been unplugged. Re-applied on every (re)open.
        struct timeval dqbuf_timeout = {};
        dqbuf_timeout.tv_sec = 1;
        if (ioctl(video_fd_, VIDIOC_S_DQBUF_TIMEOUT, &dqbuf_timeout) != 0) {
            ESP_LOGW(TAG, "Failed to set DQBUF timeout");
        }
    }

    struct v4l2_capability cap = {};
    if (ioctl(video_fd_, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        return false;
    }

    ESP_LOGD(TAG,
             "VIDIOC_QUERYCAP: driver=%s, card=%s, bus_info=%s, version=0x%08lx, "
             "capabilities=0x%08lx, device_caps=0x%08lx",
             cap.driver, cap.card, cap.bus_info, cap.version, cap.capabilities, cap.device_caps);

    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        return false;
    }
    ESP_LOGD(TAG, "VIDIOC_G_FMT: pixelformat=0x%08lx, width=%ld, height=%ld",
             format.fmt.pix.pixelformat, format.fmt.pix.width, format.fmt.pix.height);
    CAM_PRINT_FOURCC(format.fmt.pix.pixelformat);

    struct v4l2_format setformat = {};
    setformat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    sensor_width_ = format.fmt.pix.width;
    sensor_height_ = format.fmt.pix.height;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    setformat.fmt.pix.width = format.fmt.pix.width;
    setformat.fmt.pix.height = format.fmt.pix.height;

    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;
    uint32_t best_fmt = 0;
    int best_rank = 1 << 30;  // large number

    // esp_video 2.x uses the packed YUYV/UYVY FOURCC values. Keep YUV422P
    // for compatibility with esp_video 1.x, where it was used for YUYV data.
#if defined(CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE) && defined(CONFIG_SOC_PPA_SUPPORTED)
    auto get_rank = [](uint32_t fmt) -> int {
        switch (fmt) {
            case V4L2_PIX_FMT_RGB24:
                return 0;
            case V4L2_PIX_FMT_RGB565:
            case V4L2_PIX_FMT_RGB565X:  // byte-swapped to RGB565 in Capture()
                return 1;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420:  // 软件 JPEG 编码器不支持 YUV420 格式
                return 2;
#endif  // CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUYV:
            case V4L2_PIX_FMT_UYVY:
                return 3;
            case V4L2_PIX_FMT_GREY:
            case V4L2_PIX_FMT_YUV422P:
            default:
                return 1 << 29;  // unsupported
        }
    };
#else
    auto get_rank = [](uint32_t fmt) -> int {
        switch (fmt) {
            case V4L2_PIX_FMT_YUYV:
            case V4L2_PIX_FMT_UYVY:
            case V4L2_PIX_FMT_YUV422P:
                return 10;
            case V4L2_PIX_FMT_RGB565:
            case V4L2_PIX_FMT_RGB565X:  // byte-swapped to RGB565 in Capture()
                return 11;
            case V4L2_PIX_FMT_RGB24:
                return 12;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420:
                return 13;
#endif  // CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_JPEG:
                return 5;
#endif  // CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_GREY:
                return 20;
            default:
                return 1 << 29;  // unsupported
        }
    };
#endif
    while (ioctl(video_fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        ESP_LOGD(TAG, "VIDIOC_ENUM_FMT: pixelformat=0x%08lx, description=%s", fmtdesc.pixelformat,
                 fmtdesc.description);
        CAM_PRINT_FOURCC(fmtdesc.pixelformat);
        int rank = get_rank(fmtdesc.pixelformat);
        if (rank < best_rank) {
            best_rank = rank;
            best_fmt = fmtdesc.pixelformat;
        }
        fmtdesc.index++;
    }
    if (best_rank < (1 << 29)) {
        setformat.fmt.pix.pixelformat = best_fmt;
        sensor_format_ = best_fmt;
    }

    if (!setformat.fmt.pix.pixelformat) {
        ESP_LOGE(TAG, "no supported pixel format found");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return false;
    }

    ESP_LOGD(TAG, "selected pixel format: 0x%08lx", setformat.fmt.pix.pixelformat);

    if (ioctl(video_fd_, VIDIOC_S_FMT, &setformat) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return false;
    }

#if CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED
    SetHMirror(kConfiguredHMirror);
    SetVFlip(kConfiguredVFlip);
#endif

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    frame_.width = setformat.fmt.pix.height;
    frame_.height = setformat.fmt.pix.width;
#else
    frame_.width = setformat.fmt.pix.width;
    frame_.height = setformat.fmt.pix.height;
#endif

    // 申请缓冲并mmap
    struct v4l2_requestbuffers req = {};
    req.count = is_mipi_csi_ ? 2 : 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(video_fd_, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return false;
    }
    mmap_buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(video_fd_, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
            close(video_fd_);
            video_fd_ = -1;
            sensor_format_ = 0;
            return false;
        }
        void* start =
            mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd_, buf.m.offset);
        if (start == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap failed");
            close(video_fd_);
            video_fd_ = -1;
            sensor_format_ = 0;
            return false;
        }
        mmap_buffers_[i].start = start;
        mmap_buffers_[i].length = buf.length;

        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
            close(video_fd_);
            video_fd_ = -1;
            sensor_format_ = 0;
            return false;
        }
    }

    if (is_uvc_) {
        // Buffers are already queued, so the first Capture() can start the
        // stream immediately. Keeping a UVC module idle saves its sensor and
        // USB traffic power; built-in CSI/DVP sensors keep streaming.
        ESP_LOGI(TAG, "Camera init success (UVC stream stays off until capture)");
        StartStreamMonitor();
        return true;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return false;
    }

#ifdef CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
    // 当启用 ISP 时，ISP 需要一些照片来初始化参数，因此开启后后台拍摄5s照片并丢弃
    xTaskCreate(
        [](void* arg) {
            EspVideo* self = static_cast<EspVideo*>(arg);
            uint16_t capture_count = 0;
            TickType_t start = xTaskGetTickCount();
            TickType_t duration = 5000 / portTICK_PERIOD_MS;  // 5s
            while ((xTaskGetTickCount() - start) < duration) {
                struct v4l2_buffer buf = {};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                if (ioctl(self->video_fd_, VIDIOC_DQBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "VIDIOC_DQBUF failed during init");
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    continue;
                }
                if (ioctl(self->video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "VIDIOC_QBUF failed during init");
                }
                capture_count++;
            }
            ESP_LOGI(TAG, "Camera init success, captured %d frames in %lums", capture_count,
                     (unsigned long)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));
            self->streaming_on_ = true;
            self->StartStreamMonitor();
            vTaskDelete(NULL);
        },
        "CameraInitTask", 4096, this, 5, nullptr);
#else
    ESP_LOGI(TAG, "Camera init success");
    streaming_on_ = true;
    StartStreamMonitor();
#endif  // CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
    return true;
}

void EspVideo::StartStreamMonitor() {
    if (!is_uvc_ || monitor_thread_.joinable()) {
        return;
    }
    monitor_stop_ = false;
    monitor_thread_ = std::thread(&EspVideo::MonitorStream, this);
}

bool EspVideo::ReopenDevice() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (video_fd_ >= 0) {
        return true;
    }
    if (video_device_name_ == nullptr) {
        return false;
    }

    // open() re-runs the device init (and waits CONFIG_USB_UVC_INIT_TIMEOUT_MS
    // for the UVC camera), so this is what picks up a camera plugged in after
    // boot.
    int fd = open(video_device_name_, O_RDWR);
    if (fd < 0) {
        return false;
    }
    video_fd_ = fd;
    return SetupDevice();
}

bool EspVideo::StartStreamLocked() {
    if (video_fd_ < 0) {
        return false;
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGW(TAG, "STREAMON failed: %d(%s)", errno, strerror(errno));
        return false;
    }
    streaming_on_ = true;
    RefreshCameraIndicator();
    return true;
}

void EspVideo::StopStreamLocked() {
    if (video_fd_ < 0) {
        return;
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_STREAMOFF, &type) != 0 && errno != EBUSY && errno != ENODEV) {
        ESP_LOGW(TAG, "STREAMOFF failed: %d(%s)", errno, strerror(errno));
    }
    // EBUSY means the driver already considers the stream stopped; ENODEV means
    // the device vanished and nothing is running. Either way, stop tracking it.
    streaming_on_ = false;
    RefreshCameraIndicator();
}

void EspVideo::RefreshCameraIndicator() {
    // The status bar normally refreshes on the main loop's 1 s clock tick, but
    // that tick can be starved for many seconds during a conversation, which
    // would hide the brief "capturing" state. Push the update right away.
    auto display = Board::GetInstance().GetDisplay();
    if (display != nullptr) {
        display->UpdateStatusBar(true);
    }
}

bool EspVideo::RecoverCamera() {
    std::lock_guard<std::mutex> lock(device_mutex_);
    if (video_fd_ < 0) {
        return false;
    }

    // The disconnect callback closed the old UVC stream, so STREAMOFF only
    // resets the esp_video stream state (queued/done lists and buffers) that
    // was left behind when the device went away.
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_STREAMOFF, &type) != 0) {
        // EBUSY = esp_video reports ESP_ERR_INVALID_STATE: already stopped.
        if (errno != EBUSY) {
            ESP_LOGW(TAG, "STREAMOFF failed: %d(%s)", errno, strerror(errno));
            return false;
        }
    }
    streaming_on_ = false;

    if (!stream_wanted_.load()) {
        // Idle: the state is reset, and the next capture opens a fresh stream.
        ESP_LOGI(TAG, "Camera re-enumerated while idle, stream stays off");
        return true;
    }

    ESP_LOGW(TAG, "Restarting UVC stream on reconnected camera");
    if (ioctl(video_fd_, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGW(TAG, "STREAMON failed: %d(%s)", errno, strerror(errno));
        return false;
    }

    // Confirm that frames are really flowing before declaring success.
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(video_fd_, VIDIOC_DQBUF, &buf) != 0) {
        ESP_LOGW(TAG, "No frame after restart: %d(%s)", errno, strerror(errno));
        ioctl(video_fd_, VIDIOC_STREAMOFF, &type);
        return false;
    }
    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
        ESP_LOGW(TAG, "VIDIOC_QBUF after restart failed: %d(%s)", errno, strerror(errno));
    }
    streaming_on_ = true;
    return true;
}

void EspVideo::SuppressDisconnectLogs() {
    if (log_levels_saved_) {
        return;
    }
    usb_log_level_ = esp_log_level_get("usb_uvc_device");
    video_log_level_ = esp_log_level_get("esp_video");
    esp_log_level_set("usb_uvc_device", ESP_LOG_NONE);
    esp_log_level_set("esp_video", ESP_LOG_NONE);
    log_levels_saved_ = true;
}

void EspVideo::RestoreDisconnectLogs() {
    if (!log_levels_saved_) {
        return;
    }
    esp_log_level_set("usb_uvc_device", usb_log_level_);
    esp_log_level_set("esp_video", video_log_level_);
    log_levels_saved_ = false;
}

void EspVideo::MonitorStream() {
    // A camera can be absent when esp_video initializes, in which case the
    // device could not be opened. Retry every few seconds: open() re-runs the
    // UVC init, so the camera can be plugged in at any time.
    int reopen_countdown = 0;

    while (!monitor_stop_.load()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (monitor_stop_.load()) {
            break;
        }
        if (video_fd_ < 0) {
            if (is_uvc_ && --reopen_countdown <= 0) {
                reopen_countdown = 2;
                if (ReopenDevice()) {
                    RestoreDisconnectLogs();
                    camera_unplugged_ = false;
                    camera_connected_ = true;
                    recovery_attempts_ = 0;
                    ESP_LOGI(TAG, "Camera attached");
                    RefreshCameraIndicator();
                }
            }
            continue;
        }

        // Cheap, non-destructive presence probe: the UVC driver checks dev_addr
        // against the USB host's device list, so it also notices a camera that
        // was unplugged while no stream was open.
        struct v4l2_streamparm parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(video_fd_, VIDIOC_G_PARM, &parm) != 0) {
            if (camera_connected_.exchange(false)) {
                ESP_LOGW(TAG, "Camera disconnected");
                SuppressDisconnectLogs();
            }
            camera_unplugged_ = true;
            // No photo can succeed now, so do not reopen the stream on
            // reconnect unless the user asks for a new capture.
            stream_wanted_ = false;
            stream_idle_deadline_ = 0;
            streaming_on_ = false;
            RefreshCameraIndicator();
            continue;
        }

        if (camera_unplugged_.load()) {
            RestoreDisconnectLogs();
            if (RecoverCamera()) {
                recovery_attempts_ = 0;
                camera_unplugged_ = false;
                camera_connected_ = true;
                ESP_LOGI(TAG, "Camera recovered%s",
                         streaming_on_.load() ? ", stream restarted" : ", stream idle");
                RefreshCameraIndicator();
            } else {
                recovery_attempts_++;
                if (recovery_attempts_ >= 3) {
                    ESP_LOGE(TAG, "Camera recovery failed %d times, rebooting", recovery_attempts_);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                }
                ESP_LOGW(TAG, "Camera recovery failed, will retry");
                SuppressDisconnectLogs();
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
            continue;
        }

        if (!camera_connected_.exchange(true)) {
            ESP_LOGI(TAG, "Camera connected");
        }

        // Stop the UVC stream once no capture has needed it for a while. The
        // deadline is re-checked under the mutex so a capture that just started
        // (and refreshed it) is never interrupted.
        if (is_uvc_ && streaming_on_.load()) {
            TickType_t now = xTaskGetTickCount();
            TickType_t deadline = stream_idle_deadline_.load();
            if (deadline != 0 && static_cast<int32_t>(now - deadline) >= 0) {
                std::unique_lock<std::mutex> lock(device_mutex_, std::try_to_lock);
                if (lock.owns_lock() && streaming_on_.load() &&
                    stream_idle_deadline_.load() == deadline) {
                    ESP_LOGI(TAG, "Camera idle, stopping UVC stream to save power");
                    stream_wanted_ = false;
                    stream_idle_deadline_ = 0;
                    StopStreamLocked();
                }
            }
        }
    }
}

EspVideo::~EspVideo() {
    monitor_stop_ = true;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    if (streaming_on_ && video_fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(video_fd_, VIDIOC_STREAMOFF, &type);
    }
    for (auto& b : mmap_buffers_) {
        if (b.start && b.length) {
            munmap(b.start, b.length);
        }
    }
    if (video_fd_ >= 0) {
        close(video_fd_);
        video_fd_ = -1;
    }
    sensor_format_ = 0;
    esp_video_deinit();
}

void EspVideo::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool EspVideo::IsConnected() const {
    if (video_fd_ < 0) {
        return false;
    }
    if (!is_uvc_) {
        // Built-in sensors only run while streaming.
        return streaming_on_.load();
    }
    // The UVC probe stays valid while the stream is closed, so the status bar
    // can show "camera present" even when the stream is idle.
    return camera_connected_.load() && !camera_unplugged_.load();
}

bool EspVideo::IsStreaming() const {
    // True only while frames are actually flowing: for UVC that is during a
    // capture and its idle linger, so the status-bar icon reads as "camera
    // active" instead of "camera powered". A UVC camera can be unplugged without
    // STREAMON being cleared, so also require the device to still be connected.
    if (!streaming_on_.load()) {
        return false;
    }
    if (!is_uvc_) {
        return true;
    }
    return camera_connected_.load() && !camera_unplugged_.load();
}

bool EspVideo::Capture() {
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    // The stream monitor may hold the lock briefly to stop an idle stream or to
    // reopen a reconnected camera; retry instead of failing the photo for that.
    std::unique_lock<std::mutex> frame_lock(device_mutex_, std::defer_lock);
    for (int attempt = 0; attempt < 20 && !frame_lock.try_lock(); attempt++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!frame_lock.owns_lock()) {
        ESP_LOGW(TAG, "Capture skipped: camera is restarting");
        return false;
    }
    if (video_fd_ < 0 || camera_unplugged_) {
        ESP_LOGE(TAG, "Capture failed: camera unavailable (video_fd_=%d, unplugged=%d)", video_fd_,
                 static_cast<int>(camera_unplugged_.load()));
        return false;
    }

    if (!streaming_on_.load()) {
        if (!is_uvc_) {
            ESP_LOGE(TAG, "Capture failed: camera is not streaming");
            return false;
        }
        // UVC power saving: the stream is opened only for a capture.
        if (!StartStreamLocked()) {
            ESP_LOGE(TAG, "Capture failed: cannot start camera stream");
            return false;
        }
        // The first frames of a freshly opened stream are often badly exposed.
        vTaskDelay(pdMS_TO_TICKS(kStreamStartSettleMs));
    }

    // Keep the stream open while the photo is taken and uploaded, then let the
    // monitor close it once the linger expires.
    stream_wanted_ = true;
    stream_idle_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kStreamIdleTimeoutMs);

    for (int i = 0; i < 3; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(video_fd_, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_DQBUF failed");
            return false;
        }
        if (i == 2) {
            // 保存帧副本到PSRAM
            if (frame_.data) {
                heap_caps_free(frame_.data);
                frame_.data = nullptr;
                frame_.format = 0;
            }
            frame_.len = buf.bytesused;
            frame_.data =
                (uint8_t*)heap_caps_malloc(frame_.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!frame_.data) {
                ESP_LOGE(TAG, "alloc frame copy failed: need allocate %lu bytes", buf.bytesused);
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
            ESP_LOGW(TAG,
                     "mmap_buffers_[buf.index].length = %d, sensor_width = %d, sensor_height = %d",
                     mmap_buffers_[buf.index].length, sensor_width_, sensor_height_);
#else
            ESP_LOGW(TAG,
                     "mmap_buffers_[buf.index].length = %d, frame.width = %d, frame.height = %d",
                     mmap_buffers_[buf.index].length, frame_.width, frame_.height);
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
            ESP_LOG_BUFFER_HEXDUMP(TAG, mmap_buffers_[buf.index].start,
                                   MIN(mmap_buffers_[buf.index].length, 256), ESP_LOG_DEBUG);

            switch (sensor_format_) {
                case V4L2_PIX_FMT_RGB565:
                case V4L2_PIX_FMT_RGB24:
                case V4L2_PIX_FMT_YUYV:
                case V4L2_PIX_FMT_UYVY:
                case V4L2_PIX_FMT_YUV420:
                case V4L2_PIX_FMT_GREY:
#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
                case V4L2_PIX_FMT_JPEG:
#endif  // CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                {
                    auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                    auto dst16 = (uint16_t*)frame_.data;
                    size_t count = (size_t)mmap_buffers_[buf.index].length / 2;
                    for (size_t i = 0; i < count; i++) {
                        dst16[i] = __builtin_bswap16(src16[i]);
                    }
                }
#else
                    memcpy(frame_.data, mmap_buffers_[buf.index].start,
                           MIN(mmap_buffers_[buf.index].length, frame_.len));
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    frame_.format = sensor_format_;
                    break;
                case V4L2_PIX_FMT_YUV422P: {
                    // 这个格式是 422 YUYV，不是 planer
                    frame_.format = V4L2_PIX_FMT_YUYV;
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    {
                        auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                        auto dst16 = (uint16_t*)frame_.data;
                        size_t count = (size_t)mmap_buffers_[buf.index].length / 2;
                        for (size_t i = 0; i < count; i++) {
                            dst16[i] = __builtin_bswap16(src16[i]);
                        }
                    }
#else
                    memcpy(frame_.data, mmap_buffers_[buf.index].start,
                           MIN(mmap_buffers_[buf.index].length, frame_.len));
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    break;
                }
                case V4L2_PIX_FMT_RGB565X: {
                    // 大端序的 RGB565 需要转换为小端序
                    // 目前 esp_video 的大小端都会返回格式为 RGB565，不会返回格式为 RGB565X，此 case
                    // 用于未来版本兼容
                    auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                    auto dst16 = (uint16_t*)frame_.data;
                    size_t pixel_count = (size_t)frame_.width * (size_t)frame_.height;
                    for (size_t i = 0; i < pixel_count; i++) {
                        dst16[i] = __builtin_bswap16(src16[i]);
                    }
                    frame_.format = V4L2_PIX_FMT_RGB565;
                    break;
                }
                default:
                    ESP_LOGE(TAG, "unsupported sensor format: 0x%08lx", sensor_format_);
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                        ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                    }
                    return false;
            }

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
#ifndef CONFIG_SOC_PPA_SUPPORTED
            uint8_t* rotate_dst = (uint8_t*)heap_caps_aligned_alloc(
                64, frame_.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (rotate_dst == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for rotate image");
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }
            uint8_t* rotate_src = (uint8_t*)frame_.data;

            esp_imgfx_rotate_cfg_t rotate_cfg = {
                .in_res =
                    {
                        .width = static_cast<int16_t>(sensor_width_),
                        .height = static_cast<int16_t>(sensor_height_),
                    },
                .degree = IMAGE_ROTATION_ANGLE,
            };
            switch (frame_.format) {
                case V4L2_PIX_FMT_RGB565:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
                    break;
                case V4L2_PIX_FMT_YUYV:
                case V4L2_PIX_FMT_UYVY:
                    // Rotate packed YUV422 as opaque 16-bit pixels. The rotation
                    // operation only needs the pixel size and does not convert color.
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
                    break;
                case V4L2_PIX_FMT_GREY:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_Y;
                    break;
                case V4L2_PIX_FMT_RGB24:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888;
                    break;
                default:
                    ESP_LOGE(TAG, "unsupported sensor format: 0x%08lx", sensor_format_);
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                        ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                    }
                    return false;
            }
            esp_imgfx_rotate_handle_t rotate_handle = nullptr;
            esp_imgfx_err_t imgfx_err = esp_imgfx_rotate_open(&rotate_cfg, &rotate_handle);
            if (imgfx_err != ESP_IMGFX_ERR_OK || rotate_handle == nullptr) {
                ESP_LOGE(TAG, "esp_imgfx_rotate_create failed");
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            esp_imgfx_data_t rotate_input_data = {
                .data = rotate_src,
                .data_len = frame_.len,
            };
            esp_imgfx_data_t rotate_output_data = {
                .data = rotate_dst,
                .data_len = frame_.len,
            };

            imgfx_err =
                esp_imgfx_rotate_process(rotate_handle, &rotate_input_data, &rotate_output_data);
            if (imgfx_err != ESP_IMGFX_ERR_OK) {
                ESP_LOGE(TAG, "esp_imgfx_rotate_process failed");
                heap_caps_free(rotate_dst);
                rotate_dst = nullptr;
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                esp_imgfx_rotate_close(rotate_handle);
                rotate_handle = nullptr;
                return false;
            }

            frame_.data = rotate_dst;

            heap_caps_free(rotate_src);
            rotate_src = nullptr;

            esp_imgfx_rotate_close(rotate_handle);
            rotate_handle = nullptr;
#else   // CONFIG_SOC_PPA_SUPPORTED
            uint8_t* rotate_src = nullptr;

            ppa_srm_color_mode_t ppa_color_mode;
            switch (frame_.format) {
                case V4L2_PIX_FMT_RGB565:
                    rotate_src = (uint8_t*)frame_.data;
                    ppa_color_mode = PPA_SRM_COLOR_MODE_RGB565;
                    break;
                case V4L2_PIX_FMT_RGB24:
                    rotate_src = (uint8_t*)frame_.data;
                    ppa_color_mode = PPA_SRM_COLOR_MODE_RGB888;
                    break;
                case V4L2_PIX_FMT_YUYV:
                case V4L2_PIX_FMT_UYVY: {
                    ESP_LOGW(TAG,
                             "YUV422 format is not supported for PPA rotation, using software "
                             "conversion to RGB888");
                    rotate_src = (uint8_t*)heap_caps_malloc(frame_.width * frame_.height * 3,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (rotate_src == nullptr) {
                        ESP_LOGE(TAG, "Failed to allocate memory for rotate image");
                        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                            ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                        }
                        return false;
                    }
                    esp_imgfx_color_convert_cfg_t convert_cfg = {
                        .in_res = {.width = static_cast<int16_t>(frame_.width),
                                   .height = static_cast<int16_t>(frame_.height)},
                        .in_pixel_fmt = static_cast<esp_imgfx_pixel_fmt_t>(frame_.format),
                        .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888,
                    };
                    esp_imgfx_color_convert_handle_t convert_handle = nullptr;
                    esp_imgfx_err_t err =
                        esp_imgfx_color_convert_open(&convert_cfg, &convert_handle);
                    if (err != ESP_IMGFX_ERR_OK || convert_handle == nullptr) {
                        ESP_LOGE(TAG, "esp_imgfx_color_convert_open failed");
                        heap_caps_free(rotate_src);
                        rotate_src = nullptr;
                        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                            ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                        }
                        return false;
                    }
                    esp_imgfx_data_t convert_input_data = {
                        .data = frame_.data,
                        .data_len = frame_.len,
                    };
                    esp_imgfx_data_t convert_output_data = {
                        .data = rotate_src,
                        .data_len = static_cast<uint32_t>(frame_.width * frame_.height * 3),
                    };
                    err = esp_imgfx_color_convert_process(convert_handle, &convert_input_data,
                                                          &convert_output_data);
                    if (err != ESP_IMGFX_ERR_OK) {
                        ESP_LOGE(TAG, "esp_imgfx_color_convert_process failed");
                        heap_caps_free(rotate_src);
                        rotate_src = nullptr;
                        esp_imgfx_color_convert_close(convert_handle);
                        convert_handle = nullptr;
                        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                            ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                        }
                        return false;
                    }
                    esp_imgfx_color_convert_close(convert_handle);
                    convert_handle = nullptr;
                    ppa_color_mode = PPA_SRM_COLOR_MODE_RGB888;
                    heap_caps_free(frame_.data);
                    frame_.data = rotate_src;
                    frame_.len = frame_.width * frame_.height * 3;
                    break;
                }
                default:
                    ESP_LOGE(TAG, "unsupported sensor format for PPA rotation: 0x%08lx",
                             sensor_format_);
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                        ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                    }
                    return false;
            }

            uint8_t* rotate_dst = (uint8_t*)heap_caps_malloc(
                frame_.width * frame_.height * 2,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED);
            if (rotate_dst == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for rotate image");
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            ppa_client_handle_t ppa_client = nullptr;
            ppa_client_config_t client_cfg = {
                .oper_type = PPA_OPERATION_SRM,
                .max_pending_trans_num = 1,
            };
            esp_err_t err = ppa_register_client(&client_cfg, &ppa_client);
            if (err != ESP_OK || ppa_client == nullptr) {
                ESP_LOGE(TAG, "ppa_register_client failed: %d", (int)err);
                heap_caps_free(rotate_dst);
                rotate_dst = nullptr;
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            ppa_srm_rotation_angle_t ppa_angle = IMAGE_ROTATION_ANGLE;

            ppa_srm_oper_config_t srm_cfg = {};
            srm_cfg.in.buffer = (void*)rotate_src;
            srm_cfg.in.pic_w = sensor_width_;
            srm_cfg.in.pic_h = sensor_height_;
            srm_cfg.in.block_w = sensor_width_;
            srm_cfg.in.block_h = sensor_height_;
            srm_cfg.in.block_offset_x = 0;
            srm_cfg.in.block_offset_y = 0;
            srm_cfg.in.srm_cm = ppa_color_mode;

            srm_cfg.out.buffer = (void*)rotate_dst;
            srm_cfg.out.buffer_size = frame_.len;
            srm_cfg.out.pic_w = frame_.width;
            srm_cfg.out.pic_h = frame_.height;
            srm_cfg.out.block_offset_x = 0;
            srm_cfg.out.block_offset_y = 0;
            srm_cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

            // 等比例缩放 1.0
            srm_cfg.scale_x = 1.0f;
            srm_cfg.scale_y = 1.0f;
            srm_cfg.rotation_angle = ppa_angle;
            srm_cfg.mode = PPA_TRANS_MODE_BLOCKING;
            srm_cfg.user_data = nullptr;

            err = ppa_do_scale_rotate_mirror(ppa_client, &srm_cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ppa_do_scale_rotate_mirror failed: %d", (int)err);
                heap_caps_free(rotate_dst);
                rotate_dst = nullptr;
                (void)ppa_unregister_client(ppa_client);
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            (void)ppa_unregister_client(ppa_client);

            frame_.data = rotate_dst;
            frame_.len = frame_.width * frame_.height * 2;
            frame_.format = V4L2_PIX_FMT_RGB565;
            heap_caps_free(rotate_src);
            rotate_src = nullptr;
#endif  // CONFIG_SOC_PPA_SUPPORTED
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
        }

        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
        }
    }
    // Restart the linger from the end of the capture, not its start.
    stream_idle_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kStreamIdleTimeoutMs);
    frame_lock.unlock();

    // 显示预览图片
    auto display = Board::GetInstance().GetDisplay();
    if (display != nullptr && display->SupportsPreviewImage()) {
        if (!frame_.data) {
            ESP_LOGE(TAG, "frame.data is null");
            return false;
        }
        uint16_t w = frame_.width;
        uint16_t h = frame_.height;
        size_t lvgl_image_size = frame_.len;
        size_t stride = ((w * 2) + 3) & ~3;  // 4字节对齐
        lv_color_format_t color_format = LV_COLOR_FORMAT_RGB565;
        uint8_t* data = nullptr;

        switch (frame_.format) {
            // LVGL 显示 YUV 系的图像似乎都有问题，暂时转换为 RGB565 显示
            case V4L2_PIX_FMT_YUYV:
            case V4L2_PIX_FMT_UYVY:
            case V4L2_PIX_FMT_YUV420:
            case V4L2_PIX_FMT_RGB24: {
                color_format = LV_COLOR_FORMAT_RGB565;
                data = (uint8_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    ESP_LOGE(TAG, "Failed to allocate memory for preview image");
                    return false;
                }
                esp_imgfx_color_convert_cfg_t convert_cfg = {
                    .in_res = {.width = static_cast<int16_t>(frame_.width),
                               .height = static_cast<int16_t>(frame_.height)},
                    .in_pixel_fmt = static_cast<esp_imgfx_pixel_fmt_t>(frame_.format),
                    .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
                    .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
                };
                esp_imgfx_color_convert_handle_t convert_handle = nullptr;
                esp_imgfx_err_t err = esp_imgfx_color_convert_open(&convert_cfg, &convert_handle);
                if (err != ESP_IMGFX_ERR_OK || convert_handle == nullptr) {
                    ESP_LOGE(TAG, "esp_imgfx_color_convert_open failed");
                    heap_caps_free(data);
                    data = nullptr;
                    return false;
                }
                esp_imgfx_data_t convert_input_data = {
                    .data = frame_.data,
                    .data_len = frame_.len,
                };
                esp_imgfx_data_t convert_output_data = {
                    .data = data,
                    .data_len = static_cast<uint32_t>(w * h * 2),
                };
                err = esp_imgfx_color_convert_process(convert_handle, &convert_input_data,
                                                      &convert_output_data);
                if (err != ESP_IMGFX_ERR_OK) {
                    ESP_LOGE(TAG, "esp_imgfx_color_convert_process failed");
                    heap_caps_free(data);
                    data = nullptr;
                    esp_imgfx_color_convert_close(convert_handle);
                    convert_handle = nullptr;
                    return false;
                }
                esp_imgfx_color_convert_close(convert_handle);
                convert_handle = nullptr;
                lvgl_image_size = w * h * 2;
                break;
            }

            case V4L2_PIX_FMT_RGB565:
                data = (uint8_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    ESP_LOGE(TAG, "Failed to allocate memory for preview image");
                    return false;
                }
                memcpy(data, frame_.data, frame_.len);
                lvgl_image_size = frame_.len;  // fallthrough 时兼顾 YUYV 与 RGB565
                break;

#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_JPEG: {
                uint8_t* out_data = nullptr;  // out data is allocated by jpeg_to_image
                size_t out_len = 0;
                size_t out_width = 0;
                size_t out_height = 0;
                size_t out_stride = 0;

                // Decode only as large as the screen: a full-size HD frame needs a ~1.8 MB
                // buffer, which fails while the previous preview is still held. The AI
                // still receives the original frame_ in Explain().
                esp_err_t ret = jpeg_to_image_fit(frame_.data, frame_.len,
                                                  static_cast<size_t>(display->width()),
                                                  static_cast<size_t>(display->height()), &out_data,
                                                  &out_len, &out_width, &out_height, &out_stride);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to decode JPEG image: %d (%s)", (int)ret,
                             esp_err_to_name(ret));
                    if (out_data) {
                        heap_caps_free(out_data);
                        out_data = nullptr;
                    }
                    return false;
                }

                data = out_data;
                w = out_width;
                h = out_height;
                lvgl_image_size = out_len;
                stride = out_stride;
                break;
            }
#endif
            default:
                ESP_LOGE(TAG, "unsupported frame format: 0x%08lx", frame_.format);
                return false;
        }

        auto image =
            std::make_unique<LvglAllocatedImage>(data, lvgl_image_size, w, h, stride, color_format);
        display->SetPreviewImage(std::move(image));
    }
    return true;
}

bool EspVideo::SetHMirror(bool enabled) {
    if (video_fd_ < 0)
        return false;
    struct v4l2_ext_controls ctrls = {};
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_HFLIP;
    ctrl.value = enabled ? 1 : 0;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGE(TAG, "set HFLIP failed");
        return false;
    }
    return true;
}

bool EspVideo::SetVFlip(bool enabled) {
    if (video_fd_ < 0)
        return false;
    struct v4l2_ext_controls ctrls = {};
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_VFLIP;
    ctrl.value = enabled ? 1 : 0;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGE(TAG, "set VFLIP failed");
        return false;
    }
    return true;
}

namespace {

struct JpegSinkContext {
    const Camera::JpegSink* sink;
    size_t total = 0;
    bool ok = true;
};

size_t WriteJpegChunk(void* arg, size_t index, const void* data, size_t len) {
    auto* ctx = static_cast<JpegSinkContext*>(arg);
    if (index == 0 && data != nullptr && len > 0 && ctx->ok) {
        ctx->ok = (*ctx->sink)(data, len);
        if (ctx->ok) {
            ctx->total += len;
        }
    }
    return len;
}

}  // namespace

/**
 * @brief Stream the last captured frame as JPEG without uploading it.
 *
 * The frame buffer belongs to the camera and is reused by the next Capture(), so
 * this must run before another capture starts. This is the local counterpart of
 * Explain(): it lets the photo be served to a browser instead of being sent to a
 * vision model, and it streams the encoder's own buffer, so no full-size copy is
 * allocated (an HD frame is ~50 KB).
 */
std::expected<size_t, std::string> EspVideo::WriteJpeg(const JpegSink& sink, int quality) {
    if (frame_.data == nullptr || frame_.len == 0) {
        return std::unexpected("No captured frame; call Capture() first");
    }

    uint16_t w = frame_.width ? frame_.width : 320;
    uint16_t h = frame_.height ? frame_.height : 240;
    v4l2_pix_fmt_t enc_fmt = frame_.format;

    JpegSinkContext ctx = {.sink = &sink};
    bool ok = image_to_jpeg_cb(frame_.data, frame_.len, w, h, enc_fmt,
                               static_cast<uint8_t>(quality), WriteJpegChunk, &ctx);

    if (!ok || !ctx.ok || ctx.total == 0) {
        ESP_LOGE(TAG, "Failed to encode image to JPEG");
        return std::unexpected("Failed to encode image to JPEG");
    }
    ESP_LOGI(TAG, "Snapshot frame size=%d bytes, JPEG size=%d bytes", (int)frame_.len,
             (int)ctx.total);
    return ctx.total;
}

/**
 * @brief 将摄像头捕获的图像发送到远程服务器进行AI分析和解释
 *
 * 该函数将当前摄像头缓冲区中的图像编码为JPEG格式，并通过HTTP POST请求
 * 以multipart/form-data的形式发送到指定的解释服务器。服务器将根据提供的
 * 问题对图像进行AI分析并返回结果。
 *
 * 实现特点：
 * - 使用独立线程编码JPEG，与主线程分离
 * - 采用分块传输编码(chunked transfer encoding)优化内存使用
 * - 通过队列机制实现编码线程和发送线程的数据同步
 * - 支持设备ID、客户端ID和认证令牌的HTTP头部配置
 *
 * @param question 要向AI提出的关于图像的问题，将作为表单字段发送
 * @return 成功时返回服务器的 JSON 响应，失败时返回可读错误信息
 *
 * @note 调用此函数前必须先调用SetExplainUrl()设置服务器URL
 * @note 函数会等待之前的编码线程完成后再开始新的处理
 * @warning 如果摄像头缓冲区为空或网络连接失败，将返回错误信息
 */
std::expected<std::string, std::string> EspVideo::Explain(const std::string& question) {
    if (explain_url_.empty()) {
        return std::unexpected("Image explain URL or token is not set");
    }

    // 创建局部的 JPEG 队列, 40 entries is about to store 512 * 40 = 20480 bytes of JPEG data
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (jpeg_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        return std::unexpected("Failed to create JPEG queue");
    }

    // We spawn a thread to encode the image to JPEG using optimized encoder (cost about 500ms and
    // 8KB SRAM)
    encoder_thread_ = std::thread([this, jpeg_queue]() {
        uint16_t w = frame_.width ? frame_.width : 320;
        uint16_t h = frame_.height ? frame_.height : 240;
        v4l2_pix_fmt_t enc_fmt = frame_.format;
        bool ok = image_to_jpeg_cb(
            frame_.data, frame_.len, w, h, enc_fmt, 80,
            [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                auto jpeg_queue = static_cast<QueueHandle_t>(arg);
                JpegChunk chunk = {.data = nullptr, .len = len};
                if (index == 0 && data != nullptr && len > 0) {
                    chunk.data = (uint8_t*)heap_caps_aligned_alloc(
                        16, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (chunk.data == nullptr) {
                        ESP_LOGE(TAG, "Failed to allocate %zu bytes for JPEG chunk", len);
                        chunk.len = 0;
                    } else {
                        memcpy(chunk.data, data, len);
                    }
                } else {
                    chunk.len = 0;  // Sentinel or error
                }
                xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
                return len;
            },
            jpeg_queue);

        if (!ok) {
            JpegChunk chunk = {.data = nullptr, .len = 0};
            xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
        }
    });

    auto drain_jpeg_queue = [this, jpeg_queue]() {
        JpegChunk chunk;
        while (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) == pdPASS) {
            if (chunk.data != nullptr) {
                heap_caps_free(chunk.data);
            } else {
                break;
            }
        }
        encoder_thread_.join();
        vQueueDelete(jpeg_queue);
    };

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    // 构造multipart/form-data请求体
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    // 配置HTTP客户端，使用分块传输编码
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (auto opened = http->Open("POST", explain_url_); !opened) {
        ESP_LOGE(TAG, "Failed to connect to explain URL: %s", opened.error().ToString().c_str());
        drain_jpeg_queue();
        return std::unexpected("Failed to connect to explain URL");
    }

    auto write_or_fail = [&http](const char* data, size_t size) -> bool {
        if (auto written = http->Write(data, size); !written) {
            ESP_LOGE(TAG, "Failed to upload photo: %s", written.error().ToString().c_str());
            return true;
        }
        return false;
    };

    {
        // 第一块：question字段
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        if (write_or_fail(question_field.c_str(), question_field.size())) {
            drain_jpeg_queue();
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
    }
    {
        // 第二块：文件字段头部
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        if (write_or_fail(file_header.c_str(), file_header.size())) {
            drain_jpeg_queue();
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
    }

    // 第三块：JPEG数据
    size_t total_sent = 0;
    bool saw_terminator = false;
    while (true) {
        JpegChunk chunk;
        if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive JPEG chunk");
            break;
        }
        if (chunk.data == nullptr) {
            saw_terminator = true;
            break;  // The last chunk
        }
        const bool write_failed = write_or_fail((const char*)chunk.data, chunk.len);
        heap_caps_free(chunk.data);
        if (write_failed) {
            drain_jpeg_queue();
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
        total_sent += chunk.len;
    }
    // Wait for the encoder thread to finish
    encoder_thread_.join();
    // 清理队列
    vQueueDelete(jpeg_queue);

    if (!saw_terminator || total_sent == 0) {
        ESP_LOGE(TAG, "JPEG encoder failed or produced empty output");
        return std::unexpected("Failed to encode image to JPEG");
    }

    {
        // 第四块：multipart尾部
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        if (write_or_fail(multipart_footer.c_str(), multipart_footer.size())) {
            http->Close();
            return std::unexpected("Failed to upload photo");
        }
    }
    // 结束块
    if (write_or_fail("", 0)) {
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    auto status_code = http->GetStatusCode();
    if (!status_code) {
        ESP_LOGE(TAG, "Failed to read HTTP status: %s", status_code.error().ToString().c_str());
        http->Close();
        return std::unexpected("Failed to upload photo");
    }
    if (*status_code != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", *status_code);
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();

    // Get remain task stack size
    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(
        TAG,
        "Explain image size=%d bytes, compressed size=%d, remain stack size=%d, question=%s\n%s",
        (int)frame_.len, (int)total_sent, (int)remain_stack_size, question.c_str(), result.c_str());
    return result;
}
