#pragma once

// Camera-based motion detection via frame differencing at QQVGA
// grayscale. Rate-limited internally by MOTION_POLL_INTERVAL_MS.
bool initMotion();
bool motionDetected();

// Runtime camera mode switches — motion detect (grayscale QQVGA)
// vs full photo (JPEG UXGA). captureAndSendPhoto() uses these to
// swap in and out for a full-res capture.
void motionCameraForDetect();
void motionCameraForPhoto();

// Force-refresh the reference frame from whatever the camera sees
// right now — call after a photo capture, since the scene may have
// changed during the multi-second upload.
void motionResetReference();
