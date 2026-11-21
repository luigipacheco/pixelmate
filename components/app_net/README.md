# app_net

Wi-Fi (with its own first-time setup hotspot), network time and the stats web
app. Device-only; the Host simulator never compiles it.

```c
app_net_start("pixelpal");               /* once, from the game loop */
uint32_t ip;
switch (app_net_status(&ip)) {
case APP_NET_STATUS_ONLINE: /* ip in network order */ break;
case APP_NET_STATUS_NO_CONFIG:
case APP_NET_STATUS_FAILED: app_net_setup_start(); break;   /* on the user's tap */
case APP_NET_STATUS_SETUP: show(app_net_setup_name()); break;
default: break;
}
app_net_publish("{\"steps_today\":123}"); /* JSON served at /api/stats */
int minutes_west;
if (app_net_take_timezone(&minutes_west)) { /* apply + persist */ }
```

## Which network

1. A network saved by the setup hotspot: NVS namespace `app_net`, keys `ssid`
   and `pass` (strings). This component owns the namespace; it is only written
   after the board actually joined the network, so a typo is never saved.
2. Otherwise `wifi_ssid` / `wifi_password` from Vibe Mode shared values
   (`mosaico_shared_get`, read-only here), the same source FabMosaico uses.
3. Otherwise `APP_NET_STATUS_NO_CONFIG`: the radio stays off until setup.

The driver's own NVS copy is disabled (`WIFI_STORAGE_RAM`) so the app never
overwrites Wi-Fi records Vibe Mode might keep there.

## Station

Modem power save. After a drop it retries after 5 s, doubling up to every
5 minutes; a successful connection resets it. After 4 failed attempts in a row
(about 75 s) the status is `APP_NET_STATUS_FAILED` (it keeps retrying), which
is when the app offers setup again.

## Setup hotspot

`app_net_setup_start()` (from the menu's SET UP / CHANGE WIFI):

- Pauses the station and opens an **open** hotspot named `PIXELPAL-XXXX`
  (last two bytes of the soft-AP MAC, `app_net_setup_name()`) at
  `192.168.4.1`, channel 1, up to 3 phones.
- Captive DNS (`app_net_dns.c`) answers every A query with `192.168.4.1`, DHCP
  option 114 points at `http://192.168.4.1/`, and every unknown path
  redirects there, so phones pop up the setup page by themselves. If they do
  not, the board screen shows the address to type.
- The page lists nearby networks (`GET /api/scan`, strongest first, duplicates
  and hidden networks dropped), takes a password (empty for open networks,
  otherwise 8+ characters) and posts it to `POST /api/wifi`. The board tries it
  3 times; `GET /api/setup` reports `trying`, `ok` (with the new address),
  `password`, `notfound` or `failed`.
- On success the network is saved, the status turns ONLINE, and the hotspot
  stays up 20 s more so the phone can read the new address, then closes.
  Joining moves the hotspot to the home network's channel, so a phone may drop
  off it briefly; the board screen also shows ONLINE and the address.
- `app_net_setup_stop()`, or 10 minutes without success, closes the hotspot and
  goes back to the saved network (or turns the radio off if there is none).
- While setup runs, `app_net_status()` reports `APP_NET_STATUS_SETUP`; it
  switches to ONLINE as soon as the new network is joined.

The hotspot is open (no password) to keep first-time setup to two taps; it
only runs while someone asked for it on the board, and the password typed on
the page crosses that open link once. The `/api/scan`, `/api/wifi` and
`/api/setup` routes refuse to work (403) outside setup, so nothing on the home
network can change the Wi-Fi.

## Time, mDNS, web

- **Time:** SNTP (`pool.ntp.org`) starts once the station has an address and
  sets the system clock. The timezone is the caller's job (see below).
- **mDNS:** `<hostname>.local` plus an `_http._tcp` service. If Iris already
  started mDNS, the hostname is set on the existing instance.
- **Web server (port 80, both the home network and the hotspot):**
  - `GET /` – the stats page (no external assets, polls `/api/stats` every
    5 s); while setup runs, the setup page instead.
  - `GET /api/stats` – the last JSON passed to `app_net_publish`.
  - `POST /api/tz?offset=<minutes>` – the page sends JavaScript's
    `getTimezoneOffset()` on load (minutes west of UTC, −840..720).
  - Setup routes above; unknown paths are 404 outside setup.
  - At most 5 open sockets, leaving lwIP sockets for captive DNS and ESP-Iris.

## Threading

- All Wi-Fi state changes run on the default event loop task: Wi-Fi/IP events
  plus this component's own `APP_NET_EVENT` requests (setup start/stop, join,
  setup timeout). Other tasks only post requests.
- Status, address, the setup flag and the join result are read by other tasks
  under a spinlock.
- The one exception is the blocking scan in `GET /api/scan` (httpd task): it
  is read-only and only allowed while the station is paused for setup.
- The captive DNS task is started and stopped by the event loop task; stop
  waits for it to close its socket.
- `/api/stats` JSON is copied under a mutex (handlers copy it out before
  sending, so a slow client never blocks the game loop); the timezone travels
  back through a spinlocked slot.
- `app_net_start`, `app_net_publish`, `app_net_setup_start/stop` and
  `app_net_setup_name` are for the game loop task only.
- The password is wiped from request buffers and event copies once used; the
  saved copy in NVS is plain text, like Vibe Mode's own.

The server is unauthenticated and read-only apart from the timezone (and, during
setup only, the Wi-Fi form); it is meant for the home LAN.
