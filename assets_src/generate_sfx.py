#!/usr/bin/env python3
"""Generate Pixel Pal's 8-bit sound effects as deterministic 24 kHz mono WAVs.

Run by the native build (raylib_lite_native_embed_assets runs every
assets_src/generate_*.py, then packs the WAVs listed in game_assets.json into
.sound assets). Pure standard library; the same input always gives the same
bytes. Every clip stays under one second so the packer keeps it as 16-bit PCM.

Voices are the classic handheld set: pulse (square with duty), a 16-step
triangle and an LFSR noise channel. Peaks stay near 0.5 full scale; the game
picks the overall level (SOUND ON / LOUD) with the mixer's master volume.
"""
from pathlib import Path
import math
import struct
import wave

RATE = 24000
ROOT = Path(__file__).resolve().parent
MAX_FRAMES = RATE - 1          # Shorter than 1 s: packed as 16-bit, not ADPCM.


def midi(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


# Note names used below (octave 4 = middle C).
C4, D4, E4, F4, G4, A4, B4 = 60, 62, 64, 65, 67, 69, 71
C5, D5, E5, F5, G5, A5, B5 = 72, 74, 76, 77, 79, 81, 83
C6, D6, E6, G6, C7 = 84, 86, 88, 91, 96


def silence(seconds):
    return [0.0] * int(RATE * seconds)


def tone(freq, seconds, wave_kind="square", vol=0.4, duty=0.5, attack=0.002,
         decay=0.0, release=0.012, vibrato=0.0, vib_hz=7.0, slide_to=None):
    """One voice. freq in Hz; slide_to glides the pitch linearly to that Hz;
    decay is an exponential rate per second (0 = hold); vibrato in semitones."""
    count = int(RATE * seconds)
    out = []
    phase = 0.0
    lfsr = 1
    noise_value = 1.0
    noise_phase = 0.0
    for i in range(count):
        t = i / RATE
        f = freq if slide_to is None else freq + (slide_to - freq) * (i / max(1, count - 1))
        if vibrato:
            f *= 2.0 ** (vibrato * math.sin(2 * math.pi * vib_hz * t) / 12.0)
        if wave_kind == "noise":
            # 15-bit LFSR clocked at f, like the NES noise channel.
            noise_phase += f / RATE
            while noise_phase >= 1.0:
                noise_phase -= 1.0
                bit = (lfsr ^ (lfsr >> 1)) & 1
                lfsr = (lfsr >> 1) | (bit << 14)
                noise_value = 1.0 if lfsr & 1 else -1.0
            s = noise_value
        else:
            phase = (phase + f / RATE) % 1.0
            if wave_kind == "square":
                s = (1.0 if phase < duty else -1.0) - (2.0 * duty - 1.0)
            else:  # 16-step triangle
                tri = 4.0 * abs(phase - 0.5) - 1.0
                s = round(tri * 7.5) / 7.5
        env = min(1.0, t / attack) if attack > 0 else 1.0
        if decay:
            env *= math.exp(-decay * t)
        left = seconds - t
        if release and left < release:
            env *= max(0.0, left / release)
        out.append(s * env * vol)
    return out


def seq(*parts):
    out = []
    for part in parts:
        out.extend(part)
    return out


def mix(*tracks):
    """Sum tracks; each is samples or (offset_seconds, samples)."""
    placed = [(0.0, t) if not isinstance(t, tuple) else t for t in tracks]
    length = max(int(RATE * off) + len(s) for off, s in placed)
    out = [0.0] * length
    for off, samples in placed:
        start = int(RATE * off)
        for i, v in enumerate(samples):
            out[start + i] += v
    return out


def arp(notes, step, wave_kind="square", vol=0.35, duty=0.25, last=None, decay=0.0, **kw):
    """Notes back to back; the last one rings for `last` seconds."""
    parts = []
    for i, n in enumerate(notes):
        dur = last if (last and i == len(notes) - 1) else step
        parts.append(tone(midi(n), dur, wave_kind, vol, duty,
                          decay=decay if i == len(notes) - 1 else 0.0, **kw))
    return seq(*parts)


def lowpass(samples, cutoff=7000.0):
    """One-pole low-pass: takes the edge off aliasing on the small speaker."""
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    y = 0.0
    out = []
    for v in samples:
        y += a * (v - y)
        out.append(y)
    return out


# ---- The sound set ------------------------------------------------------

def step():          # very soft tick every 10 steps
    return mix(tone(1800, 0.018, "square", 0.10, 0.125, decay=160),
               tone(6000, 0.010, "noise", 0.05, decay=300))


def spark():         # classic rising coin
    return seq(tone(midi(B5), 0.055, "square", 0.32, 0.5),
               tone(midi(E6), 0.230, "square", 0.32, 0.5, decay=9))


def coins():         # sparks paid out (cash out)
    return mix(spark(), (0.11, spark()))


def fed():           # today's food walked: short victory arpeggio
    return mix(arp([C5, E5, G5, C6], 0.070, vol=0.30, duty=0.25, last=0.240, decay=6, vibrato=0.0),
               (0.0, arp([C4, G4, C5], 0.093, "triangle", vol=0.30, last=0.240, decay=6)))


def goal():          # daily goal: a bigger fanfare
    lead = seq(arp([G4, C5, E5, G5], 0.060, vol=0.28, duty=0.25),
               tone(midi(E5), 0.070, "square", 0.28, 0.25),
               tone(midi(G5), 0.070, "square", 0.28, 0.25),
               tone(midi(C6), 0.300, "square", 0.30, 0.25, decay=4, vibrato=0.25))
    bass = seq(tone(midi(C4), 0.240, "triangle", 0.32),
               tone(midi(G4), 0.140, "triangle", 0.32),
               tone(midi(C5), 0.300, "triangle", 0.32, decay=4))
    return mix(lead, bass)


def pet():           # happy chirp, "bwee-bip"
    return seq(tone(700, 0.075, "square", 0.24, 0.25, slide_to=1500),
               silence(0.015),
               tone(1300, 0.070, "square", 0.24, 0.25, slide_to=1900, decay=12))


def eat():           # crunch munch munch
    bite = mix(tone(2600, 0.050, "noise", 0.30, decay=45),
               tone(180, 0.045, "square", 0.20, 0.5, decay=50, slide_to=110))
    return seq(bite, silence(0.035), bite, silence(0.035), bite)


def buzz():          # not enough sparks / blocked: low "eh-eh"
    one = tone(150, 0.090, "square", 0.26, 0.25, decay=4)
    return seq(one, silence(0.040), one)


def tap():           # tiny UI click
    return tone(2200, 0.020, "square", 0.14, 0.25, decay=140)


def swipe():         # page swipe: a soft downward flick
    return mix(tone(1100, 0.060, "square", 0.12, 0.125, slide_to=600, decay=35),
               tone(9000, 0.050, "noise", 0.05, decay=60))


def open_menu():     # whoosh up
    return mix(tone(320, 0.130, "square", 0.14, 0.125, slide_to=980, decay=10),
               tone(3000, 0.130, "noise", 0.08, slide_to=11000, decay=14))


def close_menu():    # whoosh down
    return mix(tone(980, 0.110, "square", 0.14, 0.125, slide_to=320, decay=12),
               tone(11000, 0.110, "noise", 0.08, slide_to=3000, decay=16))


def buy():           # cash register "ka-ching"
    ka = mix(tone(5000, 0.035, "noise", 0.30, decay=70),
             tone(220, 0.035, "square", 0.18, 0.5, decay=60))
    ching = mix(tone(midi(E6), 0.060, "square", 0.26, 0.125),
                (0.060, tone(midi(A5 + 12), 0.330, "square", 0.24, 0.125, decay=8, vibrato=0.12, vib_hz=11)),
                (0.060, tone(midi(A5), 0.330, "triangle", 0.20, decay=8)))
    return seq(ka, silence(0.020), ching)


def deal():          # card / board dealt: a quick "fwip"
    return mix(tone(4000, 0.070, "noise", 0.22, slide_to=12000, decay=35),
               tone(500, 0.040, "square", 0.10, 0.125, slide_to=900, decay=50))


def flip():          # card flip: tick-tock blip
    return seq(tone(9000, 0.012, "noise", 0.20, decay=150),
               silence(0.018),
               tone(midi(A5), 0.050, "square", 0.20, 0.25, decay=40))


def win():           # small win sting
    return arp([G5, B5, D6, G6], 0.050, vol=0.27, duty=0.25, last=0.180, decay=10)


def lose():          # wah-wah
    return seq(tone(midi(E4), 0.150, "square", 0.26, 0.5, slide_to=midi(E4) * 0.94),
               silence(0.030),
               tone(midi(C4) * 1.02, 0.280, "square", 0.26, 0.5, slide_to=midi(C4) * 0.85,
                    decay=3, vibrato=0.3, vib_hz=6))


def spin():          # slot lever + reels start
    return seq(tone(3000, 0.030, "noise", 0.24, decay=80),
               tone(300, 0.110, "square", 0.16, 0.25, slide_to=700, decay=8))


def reel():          # reel stop clack
    return mix(tone(2000, 0.045, "noise", 0.30, decay=70),
               tone(260, 0.060, "square", 0.22, 0.5, decay=55, slide_to=140))


def jackpot():       # big slot win fanfare (stays under 1 s)
    run = arp([C5, E5, G5, C6, E6, G6], 0.045, vol=0.26, duty=0.25)
    trill = seq(*[tone(midi(C7 if i % 2 == 0 else G6), 0.040, "square", 0.22, 0.125)
                  for i in range(8)])
    tail = tone(midi(C7), 0.260, "square", 0.22, 0.25, decay=6, vibrato=0.2)
    bass = seq(tone(midi(C4), 0.27, "triangle", 0.30), tone(midi(G4), 0.32, "triangle", 0.30),
               tone(midi(C5), 0.27, "triangle", 0.30, decay=5))
    return mix(seq(run, trill, tail), bass)


def tile():          # grid tile flip (x1)
    return tone(midi(E5), 0.070, "square", 0.22, 0.25, decay=30, slide_to=midi(A5))


def tile_bonus():    # x2 / x3 tile: bright double blip
    return seq(tone(midi(A5), 0.050, "square", 0.24, 0.25),
               tone(midi(E6), 0.120, "square", 0.24, 0.25, decay=18))


def zap():           # dud: zap down
    return mix(tone(1400, 0.300, "square", 0.24, 0.5, slide_to=70, decay=5),
               tone(6000, 0.200, "noise", 0.16, slide_to=800, decay=12))


def clear():         # grid cleared: quick climbing run
    run = arp([C5, D5, E5, G5, A5, C6, D6, E6], 0.038, vol=0.24, duty=0.25)
    return seq(run, tone(midi(G6), 0.220, "square", 0.24, 0.25, decay=8, vibrato=0.2))


def wake():          # boot chime: "bo-ding"
    return mix(seq(tone(midi(G4), 0.090, "triangle", 0.32),
                   tone(midi(D5), 0.300, "triangle", 0.32, decay=6)),
               (0.090, tone(midi(D6), 0.260, "square", 0.12, 0.125, decay=9)))


def faint():         # sad descending
    notes = [(G4, 0.16), (F4 + 0, 0.16), (E4, 0.16), (C4 + 1, 0.30)]
    parts = [tone(midi(n), d, "square", 0.24, 0.5, slide_to=midi(n) * 0.97,
                  decay=2 if i == 3 else 0, vibrato=0.35 if i == 3 else 0.0, vib_hz=5)
             for i, (n, d) in enumerate(notes)]
    return seq(*parts)


def revive():        # rising back to life
    sweep = tone(200, 0.220, "square", 0.20, 0.25, slide_to=900)
    return seq(sweep, arp([C5, G5, C6], 0.060, vol=0.27, duty=0.25, last=0.220, decay=8))


SOUNDS = {
    "sfx_step": step, "sfx_spark": spark, "sfx_coins": coins, "sfx_fed": fed,
    "sfx_goal": goal, "sfx_pet": pet, "sfx_eat": eat, "sfx_buzz": buzz,
    "sfx_tap": tap, "sfx_swipe": swipe, "sfx_open": open_menu,
    "sfx_close": close_menu, "sfx_buy": buy, "sfx_deal": deal, "sfx_flip": flip,
    "sfx_win": win, "sfx_lose": lose, "sfx_spin": spin, "sfx_reel": reel,
    "sfx_jackpot": jackpot, "sfx_tile": tile, "sfx_tile_bonus": tile_bonus,
    "sfx_zap": zap, "sfx_clear": clear, "sfx_wake": wake, "sfx_faint": faint,
    "sfx_revive": revive,
}


# Peak level per sound (full scale = 1.0): ticks and clicks stay in the
# background, rewards stand out. Tune loudness here, not in the C table.
PEAK = {
    "sfx_step": 0.10, "sfx_tap": 0.16, "sfx_swipe": 0.18, "sfx_open": 0.26,
    "sfx_close": 0.24, "sfx_deal": 0.24, "sfx_flip": 0.26, "sfx_spin": 0.26,
    "sfx_spark": 0.36, "sfx_pet": 0.36, "sfx_buzz": 0.34, "sfx_reel": 0.38,
    "sfx_tile": 0.30, "sfx_tile_bonus": 0.36, "sfx_eat": 0.40, "sfx_coins": 0.40,
    "sfx_lose": 0.36, "sfx_faint": 0.34, "sfx_zap": 0.38, "sfx_wake": 0.40,
    "sfx_win": 0.42, "sfx_clear": 0.42, "sfx_revive": 0.42, "sfx_buy": 0.45,
    "sfx_fed": 0.45, "sfx_goal": 0.50, "sfx_jackpot": 0.50,
}


def save(name, samples):
    samples = lowpass(samples)[:MAX_FRAMES]
    peak = max(abs(v) for v in samples) or 1.0
    gain = PEAK[name] / peak
    samples = [v * gain for v in samples]
    pcm = b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 32767))))
                   for v in samples)
    with wave.open(str(ROOT / f"{name}.wav"), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm)


if __name__ == "__main__":
    for sound_name, make in SOUNDS.items():
        save(sound_name, make())
