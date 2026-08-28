#pragma once

// =====================================================
// TEMPLATE - copy this file to Secrets.h and fill it in.
//
//   cp Secrets.example.h Secrets.h
//
// Secrets.h is listed in .gitignore and must never be
// committed.
//
// WHAT BELONGS HERE
//   Only what the ESP32 physically cannot work without:
//   the Wi-Fi credentials and the address of your own
//   backend on your own LAN.
//
// WHAT MUST NEVER GO HERE
//   Spotify client secrets, access tokens, refresh
//   tokens, or any third-party API key. Those live on
//   the PC/backend. Firmware can be dumped off a board
//   over USB by anyone holding it - treat everything
//   here as readable by whoever has the device.
// =====================================================

#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

// Backend host on your LAN. An IP avoids depending on
// mDNS resolution from the ESP32.
#define BACKEND_HOST "192.168.1.100"
#define BACKEND_PORT 8080
#define BACKEND_PATH "/bot"

// Shared secret the backend checks before accepting a
// connection. This is what stops anything else on your
// network driving the bot. Generate a long random string.
#define BACKEND_TOKEN "change-me"
