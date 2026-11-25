#!/usr/bin/env python3
"""
Get a Spotify refresh token for the Spotify Hologram Cube.

Run on your computer (Python 3.8+, no extra packages needed):

    python3 get_refresh_token.py

It asks for your app's Client ID and Client Secret, opens Spotify's login
page in your browser, catches the redirect on http://127.0.0.1:8888/callback
and prints the refresh token to paste into include/config.h.

Before running: in your app's settings on developer.spotify.com, add this
exact Redirect URI and save:   http://127.0.0.1:8888/callback
"""

import base64
import getpass
import http.server
import json
import secrets
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-currently-playing user-read-playback-state user-modify-playback-state"

result = {}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/callback":
            self.send_response(404)
            self.end_headers()
            return
        qs = urllib.parse.parse_qs(parsed.query)
        result["code"] = qs.get("code", [None])[0]
        result["state"] = qs.get("state", [None])[0]
        result["error"] = qs.get("error", [None])[0]
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        msg = "Done! You can close this tab and go back to the terminal." if result["code"] else \
              f"Spotify returned an error: {result['error']}"
        self.wfile.write(f"<h2 style='font-family:sans-serif'>{msg}</h2>".encode())
        threading.Thread(target=self.server.shutdown, daemon=True).start()

    def log_message(self, *args):
        pass


def main():
    print("Spotify refresh-token helper\n")
    client_id = input("Client ID: ").strip()
    client_secret = getpass.getpass("Client Secret (hidden): ").strip()
    if not client_id or not client_secret:
        sys.exit("Client ID and Secret are both required.")

    state = secrets.token_urlsafe(16)
    auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPES,
        "state": state,
    })

    try:
        server = http.server.HTTPServer(("127.0.0.1", 8888), Handler)
    except OSError:
        sys.exit("Port 8888 is busy. Close whatever is using it and try again.")

    print("\nOpening your browser to log in to Spotify...")
    print("If it doesn't open, paste this link into your browser:\n")
    print(auth_url + "\n")
    webbrowser.open(auth_url)
    server.serve_forever()

    if result.get("error") or not result.get("code"):
        sys.exit(f"Login failed: {result.get('error')}")
    if result.get("state") != state:
        sys.exit("State mismatch - please run the script again.")

    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    body = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": result["code"],
        "redirect_uri": REDIRECT_URI,
    }).encode()
    req = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=body,
        headers={"Authorization": f"Basic {basic}",
                 "Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with urllib.request.urlopen(req) as resp:
            tokens = json.load(resp)
    except urllib.error.HTTPError as e:
        sys.exit(f"Token request failed ({e.code}): {e.read().decode()}")

    refresh = tokens.get("refresh_token")
    if not refresh:
        sys.exit(f"No refresh token in response: {tokens}")

    print("Success! Put this line in include/config.h:\n")
    print(f'#define SPOTIFY_REFRESH_TOKEN  "{refresh}"\n')
    print("Keep it private - it lets the device control your Spotify.")


if __name__ == "__main__":
    main()
