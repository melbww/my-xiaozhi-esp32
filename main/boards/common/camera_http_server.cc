#include "camera_http_server.h"

#include <esp_http_server.h>
#include <esp_log.h>

#include <cstdlib>

#include "board.h"
#include "camera.h"

#define TAG "CameraHttp"

namespace {

httpd_handle_t server = nullptr;

constexpr int kDefaultQuality = 80;
constexpr int kMaxQuality = 95;

// The page is deliberately self-contained: one button, one image, no polling.
// Auto-refreshing would make the UVC module stream (and heat up) continuously.
const char kIndexHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Camera snapshot</title>
<style>
 body { margin: 0; padding: 1rem; font-family: system-ui, sans-serif;
        background: #111; color: #eee; display: flex; flex-direction: column;
        align-items: center; gap: 1rem; }
 img { max-width: 100%; min-width: 16rem; background: #222; border-radius: 6px; }
 button { font-size: 1rem; padding: .6rem 1.2rem; border: 0; border-radius: 6px;
          background: #4b8bf5; color: #fff; }
 button:disabled { opacity: .5; }
 #meta { font-size: .85rem; color: #aaa; }
</style>
</head>
<body>
<button id="shoot">Take photo</button>
<img id="photo" alt="last photo">
<div id="meta">press the button to take a photo</div>
<script>
 const img = document.getElementById('photo');
 const meta = document.getElementById('meta');
 const shoot = document.getElementById('shoot');
 let started = 0;
 function grab() {
   shoot.disabled = true;
   meta.textContent = 'capturing...';
   started = Date.now();
   img.src = '/photo.jpg?t=' + started;
 }
 img.onload = () => {
   shoot.disabled = false;
   meta.textContent = img.naturalWidth + 'x' + img.naturalHeight + ' - ' +
                      (Date.now() - started) + ' ms';
 };
 img.onerror = () => {
   shoot.disabled = false;
   meta.textContent = 'capture failed (is the camera plugged in?)';
 };
 shoot.onclick = grab;
</script>
</body>
</html>
)HTML";

int QueryQuality(httpd_req_t* req) {
    int quality = kDefaultQuality;
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len > 63) {
        return quality;
    }
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return quality;
    }
    char value[8];
    if (httpd_query_key_value(query, "q", value, sizeof(value)) == ESP_OK) {
        int parsed = std::atoi(value);
        if (parsed >= 1 && parsed <= kMaxQuality) {
            quality = parsed;
        }
    }
    return quality;
}

esp_err_t IndexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t PhotoHandler(httpd_req_t* req) {
    auto camera = Board::GetInstance().GetCamera();
    if (camera == nullptr) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No camera on this board");
    }

    const int quality = QueryQuality(req);
    // Capture() opens the UVC stream when needed and waits for auto-exposure.
    if (!camera->Capture()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Capture failed (is the camera plugged in?)");
    }

    // Stream the encoder's own buffer straight to the socket: an HD JPEG is
    // ~50 KB and copying it into the handler task's heap is unnecessary.
    // Headers are only committed once the first chunk arrives, so a failure
    // before that can still produce a real error response.
    bool sent_any = false;
    auto written = camera->WriteJpeg(
        [req, &sent_any](const void* data, size_t len) {
            if (!sent_any) {
                httpd_resp_set_type(req, "image/jpeg");
                httpd_resp_set_hdr(req, "Cache-Control", "no-store");
                sent_any = true;
            }
            return httpd_resp_send_chunk(req, static_cast<const char*>(data), len) == ESP_OK;
        },
        quality);

    if (!sent_any) {
        const char* reason = written ? "Empty JPEG" : written.error().c_str();
        ESP_LOGE(TAG, "Snapshot failed: %s", reason);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, reason);
    }
    if (!written) {
        // The response is already committed; close the chunked body and drop it.
        ESP_LOGE(TAG, "Snapshot aborted: %s", written.error().c_str());
    }

    esp_err_t err = httpd_resp_send_chunk(req, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to finish JPEG response: %s", esp_err_to_name(err));
    }
    return err;
}

}  // namespace

bool CameraHttpServer::Start(int port) {
    if (server != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = static_cast<uint16_t>(port);
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;
    // Capture and JPEG encoding run in the handler task; the default 4 KiB
    // stack is not enough for the encoder path.
    config.stack_size = 8192;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera HTTP server on port %d", port);
        server = nullptr;
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = IndexHandler,
        .user_ctx = nullptr,
    };
    httpd_uri_t photo_uri = {
        .uri = "/photo.jpg",
        .method = HTTP_GET,
        .handler = PhotoHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &photo_uri);

    ESP_LOGI(TAG, "Camera snapshot server ready: http://<device-ip>:%d/photo.jpg", port);
    return true;
}
