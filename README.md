# Spotify Hologram Cube

A small desk gadget that shows **what's playing on your Spotify**: album art, song, artist and a progress bar. The picture appears to **float inside a glass cube**, and three physical buttons do **previous / play-pause / next**.

```
        viewer
          👀
          │
   ┌──────▼──────┐   ← 40 mm beam splitter cube
   │   ╱ album   │     (the 45° mirror inside reflects
   │  ╱  art     │      the screen towards you, so the
   │ ╱  floats   │      image seems to hang in the glass)
   └─────────────┘
 ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔  ← 2.8" ESP32 screen, facing up
   [<<]  [▶‖]  [>>]    ← buttons on the front of the base
```

It's not a true hologram. It's the **Pepper's ghost** effect: the cube has a half-mirror on its diagonal that reflects the screen underneath, while you can still see through the glass.

---

## 1. Shopping list

Prices are rough (USD) and vary by seller. In Singapore, AliExpress, Shopee and Lazada all carry these parts. Search terms are in *italics*.

| # | Part | Qty | Approx. | Notes / search terms |
|---|------|-----|---------|-------|
| 1 | **ESP32-2432S028R "Cheap Yellow Display" (CYD)**, 2.8" 320×240 | 1 | $12–18 | *ESP32-2432S028R*. Either version works: one micro-USB port, or micro-USB + USB-C ("CYD2USB"). Note which one you got. |
| 2 | **Beam splitter cube, 40 mm, 50/50 (R/T)**, K9 glass | 1 | $8–20 | *40mm beam splitter cube* or *hologram prism cube 40mm*. 40 mm fits the screen's height. **Avoid "RGB X-cube / dichroic"** cubes, which tint colours. |
| 3 | Tactile push buttons, 12 mm (or 6 mm) | 3 | $2 (pack) | *12x12 tactile switch*. Clicky caps look nice. |
| 4 | 10 kΩ resistor | 1 | <$1 | Needed for the NEXT button pin (GPIO35 has no internal pull-up). |
| 5 | JST 1.25 mm 4-pin cables | 2 | often included | Many CYD listings include them. Otherwise search *JST 1.25mm 4 pin cable*. |
| 6 | Hook-up wire + heat-shrink (or Dupont jumpers) | – | $2 | To run from the connectors to the buttons. |
| 7 | **Data** USB cable (micro-USB or USB-C to match your board) | 1 | $3 | Charge-only cables won't let you flash the board. |
| 8 | 5 V USB power adapter (1 A is plenty) | 1 | – | Any phone charger. |
| 9 | Enclosure / base | 1 | $0–10 | 3D print, black foam board, or a small project box (see §6). |
| – | **Spotify Premium** account | – | – | Spotify requires Premium for developer apps and for playback control. |

**Total: about $30–45.**

**Tools:** soldering iron (recommended, so the buttons stay put), wire strippers, hot glue or double-sided tape, and a computer (Windows, macOS or Linux).

---

## 2. Project files

```
spotify-hologram/
├── README.md                 ← you are here
├── platformio.ini            ← build settings (board, libraries, screen pins)
├── include/
│   └── config.example.h      ← copy to config.h and fill in Wi-Fi + Spotify details
├── src/
│   └── main.cpp              ← the firmware
└── tools/
    └── get_refresh_token.py  ← run once on your computer to log in to Spotify
```

Libraries (PlatformIO downloads these automatically): TFT_eSPI, TJpg_Decoder, ArduinoJson v6, and SpotifyArduino by Brian Lough.

---

## 3. Wiring

The CYD only has three free GPIO pins. They're on two small JST connectors on the board:

```
 CN1 connector            P3 connector
 ┌─────┬─────┬─────┬─────┐   ┌─────┬─────┬─────┬─────┐
 │ 3V3 │ IO27│ IO22│ GND │   │ IO21│ IO22│ IO35│ GND │
 └─────┴─────┴─────┴─────┘   └─────┴─────┴─────┴─────┘
                               (IO21 = backlight, DON'T use)
```

| Button | Pin | Wire it |
|--------|-----|---------|
| ⏮ Previous | **GPIO22** | One leg to IO22, other leg to GND |
| ⏯ Play / Pause | **GPIO27** | One leg to IO27, other leg to GND |
| ⏭ Next | **GPIO35** | One leg to IO35, other leg to GND, **plus a 10 kΩ resistor from IO35 to 3V3** |
| 🔁 Mirror mode | **BOOT** button | Already on the board, nothing to wire |

```
 3V3 ──[10kΩ]──┐
               │
 IO35 ─────────┴──[ NEXT ]── GND
 IO27 ────────────[ PLAY ]── GND
 IO22 ────────────[ PREV ]── GND
```

> ⚠️ Without the 10 kΩ pull-up, GPIO35 "floats" and the NEXT action fires randomly.

To change pins, edit the `PIN_BTN_*` lines in `config.h`.

---

## 4. Spotify setup (about 10 minutes, one time)

1. Go to **https://developer.spotify.com/dashboard** and log in with your Premium account.
2. Click **Create app**:
   - Name and description: anything (e.g. "Hologram Cube").
   - **Redirect URI:** `http://127.0.0.1:8888/callback` (exactly this; Spotify no longer accepts `localhost`).
   - Under "Which API/SDKs are you planning to use?", tick **Web API**.
   - Save.
3. Open the app's **Settings** and copy the **Client ID**, then click **View client secret** and copy that too.
4. Get your **refresh token**. On your computer, with Python 3 installed, run:
   ```bash
   cd spotify-hologram/tools
   python3 get_refresh_token.py
   ```
   Paste the Client ID and Secret when asked. A browser opens: log in and click **Agree**. The terminal then prints a line like:
   ```
   #define SPOTIFY_REFRESH_TOKEN  "AQD...long string..."
   ```
   Keep that line for the next step.

> Since February 2026, Spotify's "Development Mode" apps need the **app owner to have Premium** and allow a small number of users. That's fine here, since it's just you. If you get `403` errors, open the app's **User Management** tab and add your own Spotify email.

---

## 5. Build and flash

1. Install **VS Code**, then the **PlatformIO IDE** extension (from the Extensions panel).
2. Windows/macOS only: if the board doesn't show up as a serial port, install the **CH340 USB driver**.
3. In VS Code, use **File → Open Folder…** and choose `spotify-hologram`.
4. Copy `include/config.example.h` to **`include/config.h`** and fill in:
   - `WIFI_SSID`, `WIFI_PASSWORD` (2.4 GHz network)
   - `SPOTIFY_CLIENT_ID`, `SPOTIFY_CLIENT_SECRET`, `SPOTIFY_REFRESH_TOKEN`
   - `SPOTIFY_MARKET`: your country code, e.g. `"SG"`
5. Pick your board in `platformio.ini`:
   - Original single-USB CYD: leave `default_envs = cyd`
   - Two-USB CYD: change it to `default_envs = cyd2usb`
6. Plug in the board and click **→ Upload** (the arrow in the bottom PlatformIO toolbar).
   If uploading stalls at "Connecting…", hold the **BOOT** button on the board until it starts.
7. Click the **🔌 Serial Monitor** icon to watch the logs at 115200 baud.

On first boot you'll see *Connecting to Wi-Fi → Logging in to Spotify*, and then your song, or *Nothing playing* if Spotify is idle.

---

## 6. Assembly and alignment

1. **Place the cube** on the screen, roughly centred. The firmware draws everything inside a 220 × 220 px square in the middle of the screen, which is about the footprint of a 40 mm cube.
2. **Fix the orientation.** Look into the cube from the front. If the text is backwards or upside down, press the **BOOT** button. Each press steps through the 4 mirror/flip modes ("Mirror mode 2 / 4"…). Stop on the one that reads correctly; it's saved and survives reboots.
3. **Fine-tune the position**, if needed, with `CUBE_OFFSET_X` / `CUBE_OFFSET_Y` (pixels) and `CUBE_PX` (use 240 for a slightly bigger cube) in `config.h`, then re-upload.
4. **Build a base:**
   - *Easy:* black foam board box, about 95 × 60 × 25 mm. Cut a window in the top for the screen and three holes in the front for the buttons. The board is 86 × 50 mm.
   - *Nicer:* 3D print a stand. Searching Printables or Thingiverse for "CYD case" or "beam splitter cube CYD" turns up ready-made designs. Add three button holes to the front.
   - Glue a small square frame or 4 corner blocks around the cube so it can't slide.
   - Cover any bare screen around the cube with matte black tape, so only the cube glows.
5. **For the best illusion:** keep the background black (the firmware already does), use a dim room, and view at eye level, square-on to the cube.

---

## 7. Using it

| Control | Action |
|---------|--------|
| ⏮ PREV | Previous track |
| ⏯ PLAY | Pause if playing, resume if paused |
| ⏭ NEXT | Next track |
| BOOT | Cycle mirror/flip mode (saved) |

The device is a **remote and display only. It doesn't play audio.** Music keeps playing on your phone, PC or speaker, and the cube controls whichever device Spotify is currently using. If nothing is active, the buttons show *"No active device – open Spotify"*.

The screen checks Spotify every 3 seconds (`POLL_INTERVAL_MS`) and moves the progress bar smoothly between checks.

---

## 8. Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Missing include/config.h` when building | You skipped step 5.4: copy `config.example.h` to `config.h`. |
| Screen stays white or black | Wrong board env. Try `cyd2usb` instead of `cyd` (or the reverse). |
| Colours look inverted / negative (CYD2USB) | In `platformio.ini` under `[env:cyd2usb]`, change `-DTFT_INVERSION_OFF` to `-DTFT_INVERSION_ON`. |
| "Wi-Fi failed" | The ESP32 only sees **2.4 GHz** networks. Check the SSID and password (case-sensitive). |
| "Spotify login failed" / error 400 or 401 | The Client ID, Secret or refresh token is wrong or has been revoked. Re-run `get_refresh_token.py`. |
| Error 403 | Add your email under the app's **User Management** tab, and check the account is Premium. |
| Error 429 | Too many requests. Raise `POLL_INTERVAL_MS` to 5000. |
| Login works but album art never shows, serial shows TLS/SSL errors | Spotify changed its certificates. Set `ALLOW_INSECURE_TLS 1` in `config.h` (works, but skips certificate checks), or update the SpotifyArduino library. |
| NEXT triggers on its own | Missing 10 kΩ pull-up from IO35 to 3V3. |
| Song titles show `????` | The built-in fonts are ASCII only, so non-Latin characters (e.g. Chinese, Japanese, Korean) appear as `?`. |
| Image reversed or upside down in the cube | Press BOOT to cycle mirror modes. |
| Upload stuck at "Connecting……" | Hold BOOT while uploading. Use a data USB cable. |

---

## 9. Ideas to extend it

- Use the touch screen area outside the cube for volume or shuffle (the touch controller uses its own pins on the CYD).
- Use the on-board light sensor (GPIO34) to dim the backlight at night.
- When nothing is playing, show a clock or the ASCII aquarium instead of "Nothing playing".
- Swap the PREV/PLAY buttons for an I²C rotary encoder module on IO22/IO27 (turn = skip, press = pause, plus volume).

---

## Credits

- [SpotifyArduino](https://github.com/witnessmenow/spotify-api-arduino) and the [CYD community repo](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) by Brian Lough
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) and [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) by Bodmer
- Inspired by the beam-splitter-cube display builds by Kert Gartner (ASCII Aquarium) and OpenGhost.
