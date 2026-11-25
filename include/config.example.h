// =====================================================================
//  Spotify Hologram Cube - your settings
//  1. Copy this file to "config.h" (same folder)
//  2. Fill in the values below
//  config.h is in .gitignore so your secrets never get committed.
// =====================================================================
#pragma once

// ---------- Wi-Fi (2.4 GHz only - the ESP32 can't see 5 GHz) ----------
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"

// ---------- Spotify app (developer.spotify.com/dashboard) ----------
#define SPOTIFY_CLIENT_ID      "paste-client-id-here"
#define SPOTIFY_CLIENT_SECRET  "paste-client-secret-here"
// Get this by running tools/get_refresh_token.py on your computer
#define SPOTIFY_REFRESH_TOKEN  "paste-refresh-token-here"
// Your two-letter country code
#define SPOTIFY_MARKET         "SG"

// ---------- Buttons (wire each between the pin and GND) ----------
#define PIN_BTN_PREV    22   // CN1 / P3 connector
#define PIN_BTN_PLAY    27   // CN1 connector
#define PIN_BTN_NEXT    35   // P3 connector - input-only pin, NEEDS a 10k pull-up to 3.3V
#define PIN_BTN_MIRROR  0    // on-board BOOT button: cycles the 4 flip/mirror modes

// ---------- Cube placement (in screen pixels, landscape 320 x 240) ----------
// The CYD screen is ~5.5 px per mm. A 40 mm cube covers ~220 px.
#define CUBE_PX         220  // size of the square area under the cube (max 240)
#define CUBE_OFFSET_X   0    // nudge the content left(-) / right(+)
#define CUBE_OFFSET_Y   0    // nudge the content up(-) / down(+)

// ---------- Behaviour ----------
#define POLL_INTERVAL_MS   3000   // how often to ask Spotify what's playing
#define ACCENT_COLOR       0x1DCA  // progress bar colour (RGB565 of Spotify green #1DB954)

// Set to 1 ONLY if album art or login fails with TLS/certificate errors
// in the serial monitor. It skips certificate checks (less secure).
#define ALLOW_INSECURE_TLS 0
