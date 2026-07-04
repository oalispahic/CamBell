#pragma once

// Checks /firmware/meta, and if the server's version differs from
// FIRMWARE_VERSION, downloads and flashes /firmware/latest.bin.
// On success this reboots the device and does not return.
void checkAndPerformOTA();