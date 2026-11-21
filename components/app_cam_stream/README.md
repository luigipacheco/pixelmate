# app_cam_stream

The photo booth's Wi-Fi webcam. Device-only. A second HTTP server on **port
81** that exists only while the booth is ON AIR, so nothing is served while
off, and never during the setup hotspot (the model drops ON AIR then).

```c
app_cam_stream_set(snapshot.booth.on_air, snapshot.booth.cam_code);   /* every tick */
```

- `GET /cam?k=<code>`: MJPEG (`multipart/x-mixed-replace`), color, 240x320
  upright, about 12 fps (every other frame of the 25 fps sensor, hardware JPEG
  at quality 70 via `app_camera_hw`).
- `GET /cam.jpg?k=<code>`: one frame.
- Wrong or missing code: 403. The 4-digit code is shown on the booth screen and
  is new every time the booth goes on air.
- One viewer at a time: a stream occupies the server task, so `/cam.jpg` waits
  while someone watches. Two sockets, LRU purge. Its own control port (32771).
- Going off air clears the flag first; the stream handler notices within 15 ms
  and returns, then the server is stopped.
- While on air the Wi-Fi station is held out of deep power save
  (`app_net_set_low_latency(true)`, `WIFI_PS_MIN_MODEM`); going off air
  returns it to the battery-saving mode.

OBS: add a *Media Source*, untick *Local File*, input
`http://<board-ip>:81/cam?k=<code>` (or a *Browser* source with that URL).
