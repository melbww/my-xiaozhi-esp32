#!/usr/bin/env python3
"""Patch the managed esp_video component for USB-camera hot-plug.

Four vendor limitations prevent a replugged UVC camera from working properly:

1. `esp_video_usb_uvc_device.c` pauses the stream on
   UVC_HOST_DEVICE_DISCONNECTED but never closes it. The frame held by the
   capture buffer stays checked out, the USB device handle stays open, and the
   camera cannot re-enumerate. Fix: return the held frame(s) and call
   uvc_host_stream_close() from the disconnect callback (what Espressif's USB
   UVC example does).

2. `esp_video_init.c`'s USB host task breaks and uninstalls the USB host stack
   as soon as all devices are freed (ALL_FREE). That is exactly what happens
   when the camera is removed, so no new device can ever enumerate. Fix: keep
   the task running on ALL_FREE and only tear down on NO_CLIENTS.

3. `esp_video_usb_uvc_device.c`'s `uvc_video_stop()` calls
   `uvc_host_stream_stop()`/`close()` unconditionally. After patch 1 the stream
   handle is NULL on a reconnect, so `STREAMOFF` returns an error and the
   esp_video stream state can never be reset for a restart. Fix: skip the host
   calls when `stream_hdl` is NULL.

4. `esp_video_usb_uvc_device.c` only learns about a disconnect through the
   *stream* event callback, which cannot fire when no stream is open. With
   on-demand streaming the stream is closed most of the time, so `dev_addr`
   stayed valid after an unplug: `VIDIOC_G_PARM` kept reporting the camera as
   connected (status icon never cleared) and the connect handler could not adopt
   the replugged device, because it only fills slots with `dev_addr == 0`. Fix:
   verify `dev_addr` against the USB host device list inside
   `uvc_video_get_parm()` and reset the device state when it is gone.

`managed_components/` is generated and gitignored; rerun this script after a
clean build or component update:

    python3 my_exploration/tools/patch-esp-video-uvc-hotplug.py

The script is idempotent.
"""

from pathlib import Path
import sys

MARKER = "HOTPLUG PATCH"
REPO = Path(__file__).resolve().parents[2]
COMPONENT = REPO / "managed_components" / "espressif__esp_video"

PATCHES = [
    (
        COMPONENT / "src" / "device" / "esp_video_usb_uvc_device.c",
        """        device->dev_addr = 0;
        device->stream_index = 0;
        device->frame_info_num = 0;
        xSemaphoreTake(device->ready_sem, 0);
        break;
""",
        """        device->dev_addr = 0;
        device->stream_index = 0;
        device->frame_info_num = 0;
        xSemaphoreTake(device->ready_sem, 0);

        /* HOTPLUG PATCH: release frames still held by the capture buffer, then
         * close the stream. uvc_host_stream_close() also closes the USB device
         * handle, which is required before the camera can enumerate again after
         * it is replugged. */
        struct esp_video_buffer *buffer = CAPTURE_VIDEO_BUF(video);
        if (buffer) {
            for (int i = 0; i < CAPTURE_VIDEO_BUF_COUNT(video); i++) {
                struct esp_video_buffer_element *element = &buffer->element[i];
                if (element->priv_data) {
                    uvc_host_frame_return(event->device_disconnected.stream_hdl,
                                          (uvc_host_frame_t *)element->priv_data);
                    element->priv_data = NULL;
                }
            }
        }
        uvc_host_stream_close(event->device_disconnected.stream_hdl);
        device->stream_hdl = NULL;
        break;
""",
    ),
    (
        COMPONENT / "src" / "esp_video_init.c",
        """            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                ESP_LOGD(TAG, "USB: All devices freed");
                break;
            }
""",
        """            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                /* HOTPLUG PATCH: keep the USB host running after the camera is
                 * removed so a replugged device can enumerate again. Only the
                 * NO_CLIENTS path should tear the stack down. */
                ESP_LOGD(TAG, "USB: All devices freed, keeping host for hot-plug");
            }
""",
    ),
    (
        COMPONENT / "src" / "device" / "esp_video_usb_uvc_device.c",
        """    ESP_RETURN_ON_ERROR(uvc_host_stream_stop(device->stream_hdl), TAG, "Failed to stop UVC stream");

    /**
     * Free all cached frames to avoid UVC stream stop failed
     */
    while ((element = esp_video_get_done_element(video, V4L2_BUF_TYPE_VIDEO_CAPTURE)) != NULL) {
        uvc_host_frame_return(device->stream_hdl, (uvc_host_frame_t *)element->priv_data);
    }

    ESP_RETURN_ON_ERROR(uvc_host_stream_close(device->stream_hdl), TAG, "Failed to close UVC stream");
""",
        """    /* HOTPLUG PATCH: the disconnect callback closes the stream and clears
     * stream_hdl, so STREAMOFF must still succeed (it only resets the esp_video
     * stream state). Otherwise reconnecting can never restart capture. */
    if (device->stream_hdl) {
        ESP_RETURN_ON_ERROR(uvc_host_stream_stop(device->stream_hdl), TAG, "Failed to stop UVC stream");

        /**
         * Free all cached frames to avoid UVC stream stop failed
         */
        while ((element = esp_video_get_done_element(video, V4L2_BUF_TYPE_VIDEO_CAPTURE)) != NULL) {
            uvc_host_frame_return(device->stream_hdl, (uvc_host_frame_t *)element->priv_data);
        }

        ESP_RETURN_ON_ERROR(uvc_host_stream_close(device->stream_hdl), TAG, "Failed to close UVC stream");
        device->stream_hdl = NULL;
    }
""",
    ),
    (
        COMPONENT / "src" / "device" / "esp_video_usb_uvc_device.c",
        """static esp_err_t uvc_video_get_parm(struct esp_video *video, struct v4l2_streamparm *stream_parm, struct esp_video_stream *stream)
{
    struct uvc_video *device = VIDEO_PRIV_DATA(struct uvc_video *, video);
    struct v4l2_fract *time_per_frame = &stream_parm->parm.capture.timeperframe;

    ESP_RETURN_ON_FALSE(device->dev_addr, ESP_ERR_NOT_FOUND, TAG, "UVC device=%p is not connected", device);
""",
        """/* HOTPLUG PATCH: the disconnect event is only delivered to an open stream, so
 * dev_addr stays valid after the camera is unplugged while no stream is running
 * (the normal on-demand case). G_PARM would then keep reporting a removed camera
 * as connected, and the connect handler could not adopt the replugged device
 * because it only fills slots whose dev_addr is 0. Verify the address against
 * the USB host device list and reset the device state once it is gone. */
static bool uvc_video_is_present(struct uvc_video *device)
{
    if (!device->dev_addr) {
        return false;
    }

    uint8_t dev_addr_list[UVC_INIT_MAX_PHYSICAL_DEVICES] = {0};
    int num_of_devices = 0;
    if (usb_host_device_addr_list_fill(sizeof(dev_addr_list), dev_addr_list, &num_of_devices) != ESP_OK) {
        /* Unknown host state: keep reporting what we had instead of flapping. */
        return true;
    }

    for (int i = 0; i < num_of_devices; i++) {
        if (dev_addr_list[i] == device->dev_addr) {
            return true;
        }
    }

    ESP_LOGD(TAG, "UVC device with dev_addr %d is gone", device->dev_addr);
    device->dev_addr = 0;
    device->stream_index = 0;
    device->frame_info_num = 0;
    return false;
}

static esp_err_t uvc_video_get_parm(struct esp_video *video, struct v4l2_streamparm *stream_parm, struct esp_video_stream *stream)
{
    struct uvc_video *device = VIDEO_PRIV_DATA(struct uvc_video *, video);
    struct v4l2_fract *time_per_frame = &stream_parm->parm.capture.timeperframe;

    ESP_RETURN_ON_FALSE(uvc_video_is_present(device), ESP_ERR_NOT_FOUND, TAG, "UVC device=%p is not connected", device);
""",
    ),
]

for path, old, new in PATCHES:
    if not path.exists():
        sys.exit(f"missing {path} (run an idf.py build/component sync first)")
    text = path.read_text()
    if MARKER in text and new in text:
        print(f"already patched: {path}")
        continue
    count = text.count(old)
    if count != 1:
        sys.exit(f"{path}: expected 1 match, found {count}; refusing to patch")
    path.write_text(text.replace(old, new))
    print(f"patched: {path}")
