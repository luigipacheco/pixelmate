# app_net

Wi-Fi, network time and the stats web app. Device-only; the Host simulator
never compiles it.

```c
app_net_start("pixelpal");               /* once, from the game loop */
uint32_t ip;
if (app_net_status(&ip) == APP_NET_STATUS_ONLINE) { /* ip in network order */ }
app_net_publish("{\"steps_today\":123}"); /* JSON served at /api/stats */
int minutes_west;
if (app_net_take_timezone(&minutes_west)) { /* apply + persist */ }
```

- **Credentials:** `wifi_ssid` / `wifi_password` from Vibe Mode shared values
  (`mosaico_shared_get`), the same source FabMosaico uses. No setup portal.
  Missing SSID → `APP_NET_STATUS_NO_CONFIG` and `ESP_ERR_NOT_FOUND`.
- **Wi-Fi:** station mode with modem power save; reconnects 5 s after a drop.
- **Time:** SNTP (`pool.ntp.org`) starts once the station has an address and
  sets the system clock. The timezone is the caller's job (see below).
- **mDNS:** `<hostname>.local` plus an `_http._tcp` service. If Iris already
  started mDNS, the hostname is set on the existing instance.
- **Web server (port 80):**
  - `GET /` – a single self-contained page (no external assets) that polls
    `/api/stats` every 5 s.
  - `GET /api/stats` – the last JSON passed to `app_net_publish`.
  - `POST /api/tz?offset=<minutes>` – the page sends JavaScript's
    `getTimezoneOffset()` on load (minutes west of UTC, −840..720).
- **Threading:** status is written by the default event loop task and read
  under a spinlock; the JSON buffer is copied under a mutex (handlers copy it
  out before sending, so a slow client never blocks the game loop); the
  timezone travels back through a spinlocked slot. `app_net_start` and
  `app_net_publish` are for the game loop task only.
- The server is unauthenticated and read-only apart from the timezone; it is
  meant for the home LAN.
