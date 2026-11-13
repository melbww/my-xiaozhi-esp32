#ifndef CAMERA_HTTP_SERVER_H
#define CAMERA_HTTP_SERVER_H

/**
 * Exposes the camera over plain HTTP on the local network, so a photo can be
 * pulled on demand without going through the vision model:
 *
 *   http://<device-ip>/           tiny viewer page
 *   http://<device-ip>/photo.jpg  take one photo and return it as JPEG
 *
 * Unlike self.camera.take_photo, nothing is uploaded: the frame is captured,
 * JPEG-encoded and handed straight to the HTTP client.
 *
 * Start() is idempotent and must run after the network stack is initialized.
 */
class CameraHttpServer {
public:
    static bool Start(int port = 80);
};

#endif  // CAMERA_HTTP_SERVER_H
