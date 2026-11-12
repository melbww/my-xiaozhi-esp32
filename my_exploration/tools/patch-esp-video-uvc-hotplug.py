#!/usr/bin/env python3
"""Patch the managed esp_video component for USB-camera hot-plug.

Two vendor limitations prevent a replugged UVC camera from working:

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
