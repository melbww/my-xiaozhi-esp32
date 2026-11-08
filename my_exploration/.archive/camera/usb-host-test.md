# Camera USB-Host Test and Detailed Formats (Archived)

> Historical camera exploration from 2026-11-08. Current camera status and
> next steps: [../../camera/README.md](../../camera/README.md).
> Paths below refer to the pre-archive layout.

## USB host test with esp-iot-solution (2026-11-08)

Espressif's USB AV demo was used to test the camera independently of the
XiaoZhi firmware:

```
https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/host/usb_camera_mic_spk
```

It is a USB host demo with a browser console (SoftAP `USB-AV-DEMO-<MAC>`,
`http://192.168.4.1`) that detects UVC cameras (and UAC mics/speakers) and
previews MJPEG. Unlike `esp_video`, it does not block boot when nothing is
plugged in.

### Build recipe (ESP-IDF 6.1, board-safe)

```sh
git clone --depth 1 https://github.com/espressif/esp-iot-solution.git ~/esp-iot-solution
cd ~/esp-iot-solution/examples/usb/host/usb_camera_mic_spk
```

Create `sdkconfig.board.qebabe`:

```
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=n
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

```sh
source ~/esp/esp-idf/export.sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.board.qebabe" set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5D300345091 flash
```

**Do not enable `CONFIG_ESP32_S3_USB_OTG`** (Example Configuration → board
choice). That option compiles board init code that drives GPIO12/13/17/18; on
this robot GPIO12/13 are the L298N `IN1`/`IN2` motor inputs and GPIO18 is the
lamp. Keep the default `BOARD_GENERIC`.

### Diagnostic patch

`app_usb_host.c` was changed to poll the bus so "no device" is explicit:

```c
ret = usb_host_lib_handle_events(pdMS_TO_TICKS(500), &event_flags);
if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) { ... }

uint8_t addr_list[8] = {0};
int num_devices = 0;
if (usb_host_device_addr_list_fill(sizeof(addr_list), addr_list, &num_devices) == ESP_OK &&
        num_devices != last_num_devices) {
    last_num_devices = num_devices;
    if (num_devices == 0) {
        ESP_LOGW(TAG, "USB bus: no devices enumerated");
    } else {
        for (int i = 0; i < num_devices; i++) {
            ESP_LOGW(TAG, "USB bus: device %d at address %d", i + 1, addr_list[i]);
        }
    }
}
```

### Observed result

With the camera in the USB/OTG port (either plug orientation):

```
I (932) app_usb_host: USB Host installed
I (932) app_uvc: UVC host installed
I (932) uac-host: Install Succeed, Version: 1.5.0
I (933) app_uac: UAC host installed
I (1083) app_wifi: SoftAP started, SSID:USB-AV-DEMO-3ECE51 URL:http://192.168.4.1
W (1432) app_usb_host: USB bus: no devices enumerated
```

No `Handling UVC connect`, no `Pick UVC MJPEG`, no enumeration error — the host
controller never sees a D+ pull-up, i.e. the camera is unpowered.

Control experiment: same firmware, Mac USB-C cable in the USB/OTG port →
macOS shows `USB JTAG_serial debug unit` and `/dev/cu.usbmodem101`, so the port
and its data lines are healthy.

### Serial capture helper

`my_exploration/tools/serial_capture.py <port> <seconds>` pulses DTR/RTS to
reset the board (CH343 circuit) and prints the boot log; useful because
`idf.py monitor` is interactive.

## Formats and resolution

The camera supports **YUV/MJPEG** at **1280×720 / 480×320**.

`EspVideo` format ranking (from `esp_video.cc`):

| Rank | Format |
|---|---|
| 5 | JPEG (only if `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT`) |
| 10 | YUYV / UYVY / YUV422P |
| 11 | RGB565 / RGB565X |
| 12 | RGB24 |
| 20 | GREY |

`V4L2_PIX_FMT_MJPEG` is not explicitly ranked. If the camera only offers MJPEG
at 1280×720, the firmware may need MJPEG handling or a forced lower resolution
(e.g. 640×480 YUYV). YUYV 1280×720 is ~1.8 MB/frame and may be heavy for USB
2.0 + memory bandwidth, so a lower resolution or MJPEG is preferable for a
snapshot.

The `EspVideo` constructor uses the UVC driver's current format; there is no
explicit `VIDIOC_S_FMT` preference in the board code yet.
