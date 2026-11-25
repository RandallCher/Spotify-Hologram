/*
 * Spotify Hologram Cube
 * ---------------------
 * Shows what's playing on your Spotify account (album art, song, artist,
 * progress bar) on an ESP32 "Cheap Yellow Display", under a beam splitter
 * cube so it looks like it floats inside the glass.
 *
 * Buttons: PREV / PLAY-PAUSE / NEXT, plus the on-board BOOT button to cycle
 * through the 4 mirror/flip modes (saved, survives reboot).
 *
 * Settings live in include/config.h (copy config.example.h).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>
#include <SpotifyArduino.h>
#include <SpotifyArduinoCert.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Missing include/config.h - copy include/config.example.h to include/config.h and fill it in."
#endif

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int ART_BOX = 150;  // album art is drawn at max 150 x 150

static int regionX, regionY;     // top-left of the area under the cube
static int artX, artY;
static int titleY, artistY, barY;
static const int BAR_H = 4;
static const int ICON_W = 12;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
WiFiClientSecure client;
SpotifyArduino spotify(client, SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN);
Preferences prefs;

struct NowPlaying {
  bool valid = false;
  bool isPlaying = false;
  char track[SPOTIFY_NAME_CHAR_LENGTH] = "";
  char artist[SPOTIFY_NAME_CHAR_LENGTH] = "";
  char imgUrlMed[SPOTIFY_URL_CHAR_LENGTH + 10] = "";   // ~300 px
  char imgUrlSmall[SPOTIFY_URL_CHAR_LENGTH + 10] = ""; // ~64 px
  long progressMs = 0;
  long durationMs = 0;
  unsigned long fetchedAtMs = 0;
};

NowPlaying np;
char shownImgUrl[SPOTIFY_URL_CHAR_LENGTH + 10] = "";
char shownTrack[SPOTIFY_NAME_CHAR_LENGTH] = "";
char shownArtist[SPOTIFY_NAME_CHAR_LENGTH] = "";

uint8_t *jpgBuf = nullptr;  // last album art JPEG, kept so we can redraw without downloading
int jpgLen = 0;

uint8_t mirrorMode = 0;
unsigned long nextPollAt = 0;
unsigned long nextBarAt = 0;
unsigned long toastUntil = 0;
int failCount = 0;
enum Screen { SCR_NONE, SCR_IDLE, SCR_PLAYING, SCR_MESSAGE } screen = SCR_NONE;

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
struct Button {
  uint8_t pin;
  bool needsExternalPullup;
  bool stable;          // debounced level (true = released / HIGH)
  bool lastRead;
  unsigned long changedAt;
};

Button btnPrev   = {PIN_BTN_PREV,   false, true, true, 0};
Button btnPlay   = {PIN_BTN_PLAY,   false, true, true, 0};
Button btnNext   = {PIN_BTN_NEXT,   PIN_BTN_NEXT >= 34, true, true, 0};  // GPIO34-39 have no pull-ups
Button btnMirror = {PIN_BTN_MIRROR, false, true, true, 0};

void initButton(Button &b) {
  pinMode(b.pin, b.needsExternalPullup ? INPUT : INPUT_PULLUP);
  b.stable = b.lastRead = digitalRead(b.pin);
}

// Returns true once, on the moment the button is pressed down.
bool pressed(Button &b) {
  bool r = digitalRead(b.pin);
  unsigned long now = millis();
  if (r != b.lastRead) {
    b.lastRead = r;
    b.changedAt = now;
  }
  if (now - b.changedAt > 30 && r != b.stable) {
    b.stable = r;
    if (r == LOW) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Built-in fonts are ASCII only. Replace each non-ASCII character with '?'.
void toAscii(const char *in, char *out, size_t outSize) {
  size_t o = 0;
  for (size_t i = 0; in && in[i] && o + 1 < outSize; i++) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) out[o++] = (char)c;
    else if ((c & 0xC0) == 0xC0) out[o++] = '?';  // first byte of a UTF-8 char
    // continuation bytes (10xxxxxx) are skipped
  }
  out[o] = '\0';
}

// Shorten a string with "..." until it fits maxW pixels in the current font.
String fitText(const char *s, int maxW) {
  String t = s;
  if (tft.textWidth(t) <= maxW) return t;
  while (t.length() > 1 && tft.textWidth(t + "...") > maxW) t.remove(t.length() - 1);
  return t + "...";
}

void computeLayout() {
  int px = constrain(CUBE_PX, 160, 240);
  regionX = (SCREEN_W - px) / 2 + CUBE_OFFSET_X;
  regionY = (SCREEN_H - px) / 2 + CUBE_OFFSET_Y;
  artX = regionX + (px - ART_BOX) / 2;
  artY = regionY + 4;
  titleY = artY + ART_BOX + 8;
  artistY = titleY + 22;
  barY = artistY + 24;
}

int regionW() { return constrain(CUBE_PX, 160, 240); }

// The four landscape orientations: normal, flipped left-right, flipped
// up-down, and both (= rotated 180). One of them will read correctly
// through your cube. Written straight to the panel's MADCTL register so it
// works on both the ILI9341 and ST7789 versions of the board.
void applyMirror() {
  static const uint8_t modes[4] = {
    TFT_MAD_MV,
    TFT_MAD_MV | TFT_MAD_MX,
    TFT_MAD_MV | TFT_MAD_MY,
    TFT_MAD_MV | TFT_MAD_MX | TFT_MAD_MY,
  };
  tft.setRotation(1);  // sets width/height for landscape
  tft.writecommand(TFT_MADCTL);
  tft.writedata(modes[mirrorMode % 4] | TFT_MAD_COLOR_ORDER);
}

bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void showMessage(const char *line1, const char *line2 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeSansBold9pt7b);
  int cx = regionX + regionW() / 2;
  int cy = regionY + regionW() / 2;
  tft.drawString(fitText(line1, regionW() - 8), cx, cy - 12);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(fitText(line2, regionW() - 8), cx, cy + 12);
  screen = SCR_MESSAGE;
  shownImgUrl[0] = shownTrack[0] = shownArtist[0] = '\0';
}

void drawArtPlaceholder() {
  tft.fillRect(artX, artY, ART_BOX, ART_BOX, TFT_BLACK);
  tft.drawRect(artX, artY, ART_BOX, ART_BOX, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("~ ~ ~", artX + ART_BOX / 2, artY + ART_BOX / 2);
}

void drawStoredArt() {
  tft.fillRect(artX, artY, ART_BOX, ART_BOX, TFT_BLACK);
  if (!jpgBuf || jpgLen <= 0) { drawArtPlaceholder(); return; }

  uint16_t w = 0, h = 0;
  if (TJpgDec.getJpgSize(&w, &h, jpgBuf, jpgLen) != JDR_OK || w == 0) { drawArtPlaceholder(); return; }
  uint8_t scale = 1;
  while (scale < 8 && (w / scale > ART_BOX || h / scale > ART_BOX)) scale *= 2;
  TJpgDec.setJpgScale(scale);
  int dx = artX + (ART_BOX - w / scale) / 2;
  int dy = artY + (ART_BOX - h / scale) / 2;
  TJpgDec.drawJpg(dx, dy, jpgBuf, jpgLen);
}

void downloadAndDrawArt() {
  // Pick the ~300px image if there's room in memory, otherwise the 64px one.
  char *url = np.imgUrlMed;
  if (url[0] == '\0' || ESP.getMaxAllocHeap() < 70000) url = np.imgUrlSmall;
  if (url[0] == '\0') { drawArtPlaceholder(); return; }

  if (jpgBuf) { free(jpgBuf); jpgBuf = nullptr; jpgLen = 0; }

  uint8_t *buf = nullptr;
  int len = 0;
  if (spotify.getImage(url, &buf, &len) && buf && len > 0) {
    jpgBuf = buf;
    jpgLen = len;
  } else if (buf) {
    free(buf);
  }
  drawStoredArt();
}

void drawStatusIcon(bool playing, uint16_t color) {
  int x = regionX + 6;
  int y = barY - 3;
  tft.fillRect(x, y, ICON_W, 10, TFT_BLACK);
  if (playing) {
    tft.fillTriangle(x, y, x, y + 9, x + 8, y + 4, color);
  } else {
    tft.fillRect(x, y, 3, 10, color);
    tft.fillRect(x + 5, y, 3, 10, color);
  }
}

void drawProgress() {
  if (!np.valid || screen != SCR_PLAYING || millis() < toastUntil) return;
  long pos = np.progressMs;
  if (np.isPlaying) pos += (long)(millis() - np.fetchedAtMs);
  if (np.durationMs > 0) pos = constrain(pos, 0L, np.durationMs);

  int x = regionX + 6 + ICON_W + 6;
  int w = regionW() - (ICON_W + 18);
  int filled = np.durationMs > 0 ? (int)((int64_t)w * pos / np.durationMs) : 0;

  uint16_t c = np.isPlaying ? ACCENT_COLOR : TFT_DARKGREY;
  drawStatusIcon(np.isPlaying, c);
  tft.fillRect(x, barY, filled, BAR_H, c);
  tft.fillRect(x + filled, barY, w - filled, BAR_H, 0x2104);  // dark grey track
}

void toast(const char *text) {
  int y = barY - 6;
  tft.fillRect(regionX, y, regionW(), 16, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString(text, regionX + regionW() / 2, y + 8);
  toastUntil = millis() + 900;
}

void clearToastIfDone() {
  if (toastUntil && millis() >= toastUntil) {
    toastUntil = 0;
    tft.fillRect(regionX, barY - 6, regionW(), 16, TFT_BLACK);
    drawProgress();
  }
}

void drawText() {
  int w = regionW() - 8;
  int cx = regionX + regionW() / 2;
  tft.fillRect(regionX, titleY - 2, regionW(), artistY - titleY + 22, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(fitText(np.track, w), cx, titleY);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(fitText(np.artist, w), cx, artistY);
}

void render() {
  if (!np.valid) return;
  if (screen != SCR_PLAYING) {
    tft.fillScreen(TFT_BLACK);
    screen = SCR_PLAYING;
    shownImgUrl[0] = shownTrack[0] = shownArtist[0] = '\0';
  }
  if (strcmp(shownTrack, np.track) != 0 || strcmp(shownArtist, np.artist) != 0) {
    drawText();
    strlcpy(shownTrack, np.track, sizeof(shownTrack));
    strlcpy(shownArtist, np.artist, sizeof(shownArtist));
  }
  const char *want = np.imgUrlMed[0] ? np.imgUrlMed : np.imgUrlSmall;
  if (strcmp(shownImgUrl, want) != 0) {
    downloadAndDrawArt();
    strlcpy(shownImgUrl, want, sizeof(shownImgUrl));
  }
  drawProgress();
}

void redrawAll() {
  tft.fillScreen(TFT_BLACK);
  if (screen == SCR_PLAYING && np.valid) {
    drawStoredArt();
    drawText();
    drawProgress();
  } else if (screen == SCR_IDLE) {
    showMessage("Nothing playing", "Start Spotify on any device");
    screen = SCR_IDLE;
  }
}

// ---------------------------------------------------------------------------
// Spotify
// ---------------------------------------------------------------------------
void onCurrentlyPlaying(CurrentlyPlaying cp) {
  // Strings in cp are freed after this callback returns, so copy them now.
  toAscii(cp.trackName, np.track, sizeof(np.track));

  char artists[SPOTIFY_NAME_CHAR_LENGTH * 2] = "";
  for (int i = 0; i < cp.numArtists && i < 2; i++) {
    if (i) strlcat(artists, ", ", sizeof(artists));
    strlcat(artists, cp.artists[i].artistName, sizeof(artists));
  }
  toAscii(artists, np.artist, sizeof(np.artist));

  np.imgUrlMed[0] = np.imgUrlSmall[0] = '\0';
  int bestMed = -1, bestSmall = 100000;
  for (int i = 0; i < cp.numImages; i++) {
    int w = cp.albumImages[i].width;
    const char *u = cp.albumImages[i].url;
    if (!u) continue;
    if (w <= 320 && w > bestMed && w > 100) { bestMed = w; strlcpy(np.imgUrlMed, u, sizeof(np.imgUrlMed)); }
    if (w < bestSmall) { bestSmall = w; strlcpy(np.imgUrlSmall, u, sizeof(np.imgUrlSmall)); }
  }

  np.isPlaying = cp.isPlaying;
  np.progressMs = cp.progressMs;
  np.durationMs = cp.durationMs;
  np.fetchedAtMs = millis();
  np.valid = true;
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  showMessage("Connecting to Wi-Fi", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    if (millis() - start > 20000) {
      showMessage("Wi-Fi failed", "Check config.h (2.4 GHz only)");
      delay(5000);
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      start = millis();
    }
  }
  Serial.print("Wi-Fi OK, IP: ");
  Serial.println(WiFi.localIP());
}

void poll() {
  ensureWifi();
  int status = spotify.getCurrentlyPlaying(onCurrentlyPlaying, SPOTIFY_MARKET);
  Serial.printf("currently-playing -> %d (heap %u)\n", status, ESP.getFreeHeap());

  if (status == 200) {
    failCount = 0;
    render();
  } else if (status == 204) {
    failCount = 0;
    np.valid = false;
    if (screen != SCR_IDLE) {
      showMessage("Nothing playing", "Start Spotify on any device");
      screen = SCR_IDLE;
    }
  } else {
    failCount++;
    if (failCount == 3) {
      char code[24];
      snprintf(code, sizeof(code), "Spotify error %d", status);
      showMessage(code, status == 401 || status == 400 ? "Check token in config.h" : "Retrying...");
    }
  }
}

// ---------------------------------------------------------------------------
// Button actions
// ---------------------------------------------------------------------------
void afterCommand(bool ok, const char *okText) {
  if (ok) toast(okText);
  else toast("No active device - open Spotify");
  nextPollAt = millis() + 500;  // give Spotify a moment, then refresh
}

void handleButtons() {
  if (pressed(btnPrev)) {
    toast("<<  Previous");
    afterCommand(spotify.previousTrack(), "<<  Previous");
  }
  if (pressed(btnNext)) {
    toast("Next  >>");
    afterCommand(spotify.nextTrack(), "Next  >>");
  }
  if (pressed(btnPlay)) {
    bool ok;
    if (np.valid && np.isPlaying) {
      toast("Pause");
      ok = spotify.pause();
      if (ok) { np.progressMs += millis() - np.fetchedAtMs; np.fetchedAtMs = millis(); np.isPlaying = false; }
      afterCommand(ok, "Paused");
    } else {
      toast("Play");
      ok = spotify.play();
      if (ok) { np.fetchedAtMs = millis(); np.isPlaying = true; }
      afterCommand(ok, "Playing");
    }
  }
  if (pressed(btnMirror)) {
    mirrorMode = (mirrorMode + 1) % 4;
    prefs.putUChar("mirror", mirrorMode);
    applyMirror();
    redrawAll();
    char t[24];
    snprintf(t, sizeof(t), "Mirror mode %d / 4", mirrorMode + 1);
    toast(t);
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\nSpotify Hologram Cube starting");

  prefs.begin("holo", false);
  mirrorMode = prefs.getUChar("mirror", 0) % 4;

  tft.init();
  applyMirror();
  tft.fillScreen(TFT_BLACK);
  computeLayout();

  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpgOutput);

  initButton(btnPrev);
  initButton(btnPlay);
  initButton(btnNext);
  initButton(btnMirror);

  if (strncmp(SPOTIFY_REFRESH_TOKEN, "paste-", 6) == 0) {
    showMessage("No refresh token yet", "Run tools/get_refresh_token.py");
    while (true) {
      if (pressed(btnMirror)) {  // still let you pick the mirror mode
        mirrorMode = (mirrorMode + 1) % 4;
        prefs.putUChar("mirror", mirrorMode);
        applyMirror();
        showMessage("No refresh token yet", "Run tools/get_refresh_token.py");
      }
      delay(20);
    }
  }

#if ALLOW_INSECURE_TLS
  client.setInsecure();
#else
  client.setCACert(spotify_server_cert);
#endif

  ensureWifi();
  showMessage("Logging in to Spotify", "");
  if (!spotify.refreshAccessToken()) {
    showMessage("Spotify login failed", "Check ID / secret / token");
    Serial.println("refreshAccessToken failed - see README troubleshooting");
    delay(5000);
  }
  nextPollAt = 0;
}

void loop() {
  handleButtons();
  clearToastIfDone();

  unsigned long now = millis();
  if (now >= nextPollAt) {
    poll();
    nextPollAt = millis() + POLL_INTERVAL_MS;
  }
  if (now >= nextBarAt) {
    drawProgress();
    nextBarAt = now + 500;
  }
  delay(5);
}
