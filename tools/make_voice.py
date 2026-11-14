#!/usr/bin/env python3
"""Generate the ParaVario voice pack (data/voice.pak, all the clips in one file) from tools/vocab.txt.

Speech comes from the Windows built-in speech engine (SAPI) unless a recording exists at
tools/voice_custom/<id>.wav, which always wins. Each clip is 16 kHz mono IMA ADPCM:
  "IMA1" | u32 sample rate | u32 sample count | 4-bit codes, low nibble first.
They are stored in one file, data/voice.pak: "PVP1", then for each clip 20 bytes of name (zero padded), a u32 size and the clip
(see src/audio/VoicePack.h). One file instead of one per clip saves about a quarter of the flash, which the filesystem would
spend rounding every clip up to whole 4 kB blocks.

Usage:
  python tools/make_voice.py                  # build all clips with the default voice
  python tools/make_voice.py --list-voices
  python tools/make_voice.py --voice "Microsoft Hazel Desktop" --rate 1
Then flash the filesystem: pio run -t uploadfs
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VOCAB = os.path.join(ROOT, "tools", "vocab.txt")
CUSTOM_DIR = os.path.join(ROOT, "tools", "voice_custom")
OUT_FILE = os.path.join(ROOT, "data", "voice.pak")
OLD_DIR = os.path.join(ROOT, "data", "v")  # the old layout, removed
RATE = 11025  # the clips are stored at this rate and played up to the 16 kHz output (src/audio/Resample.h): about two thirds the size; --rate-hz changes it

INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767,
]

PS_SYNTH = r"""
param([string]$JobFile, [string]$Voice, [int]$Rate)
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
if ($Voice) { $s.SelectVoice($Voice) }
$s.Rate = $Rate
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,
  [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
foreach ($job in (Get-Content -Raw -Encoding UTF8 $JobFile | ConvertFrom-Json)) {
  $s.SetOutputToWaveFile($job.path, $fmt)
  $s.Speak($job.text)
}
$s.SetOutputToNull()
"""

PS_LIST = r"""
Add-Type -AssemblyName System.Speech
(New-Object System.Speech.Synthesis.SpeechSynthesizer).GetInstalledVoices() |
  ForEach-Object { $_.VoiceInfo.Name + ' (' + $_.VoiceInfo.Culture + ')' }
"""


def run_ps(script, *args):
    with tempfile.NamedTemporaryFile("w", suffix=".ps1", delete=False, encoding="utf-8") as f:
        f.write(script)
        path = f.name
    try:
        return subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", path, *args],
            check=True, capture_output=True, text=True,
        ).stdout
    finally:
        os.remove(path)


def read_vocab():
    items = []
    with open(VOCAB, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            clip, text = line.split("|", 1)
            items.append((clip.strip(), text.strip()))
    return items


def read_wav(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: must be 16-bit PCM")
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if ch > 1:
        samples = [sum(samples[i:i + ch]) // ch for i in range(0, len(samples), ch)]
    if rate != RATE:
        ratio = rate / RATE
        out_n = int(len(samples) / ratio)
        resampled = []
        for i in range(out_n):
            pos = i * ratio
            j = int(pos)
            frac = pos - j
            a = samples[j]
            b = samples[min(j + 1, len(samples) - 1)]
            resampled.append(int(a + (b - a) * frac))
        samples = resampled
    return list(samples)


def trim_and_normalise(samples, pad_ms=20):
    if not samples:
        return samples
    peak = max(abs(s) for s in samples) or 1
    thresh = peak * 0.03
    start = next((i for i, s in enumerate(samples) if abs(s) > thresh), 0)
    end = len(samples) - next((i for i, s in enumerate(reversed(samples)) if abs(s) > thresh), 0)
    pad = RATE * pad_ms // 1000
    samples = samples[max(0, start - pad):min(len(samples), end + pad)]
    gain = 0.9 * 32767 / peak
    return [max(-32768, min(32767, int(s * gain))) for s in samples]


def adpcm_encode(samples):
    pred, index = 0, 0
    out = bytearray()
    low = None
    for s in samples:
        step = STEP_TABLE[index]
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        if diff >= step:
            code |= 4
            diff -= step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
        if diff >= step >> 2:
            code |= 1
        # Track the predictor exactly as the decoder will.
        d = step >> 3
        if code & 4:
            d += step
        if code & 2:
            d += step >> 1
        if code & 1:
            d += step >> 2
        pred = pred - d if code & 8 else pred + d
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        if low is None:
            low = code
        else:
            out.append(low | (code << 4))
            low = None
    if low is not None:
        out.append(low)
    return bytes(out)


def main():
    global RATE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--voice", default="Microsoft Hazel Desktop",
                    help="SAPI voice name (see --list-voices); empty string = system default")
    ap.add_argument("--rate", type=int, default=1, help="SAPI speaking rate -10..10")
    ap.add_argument("--list-voices", action="store_true")
    ap.add_argument("--rate-hz", type=int, default=RATE, help="sample rate the clips are stored at, 8000..16000 (smaller = less flash, duller voice)")
    args = ap.parse_args()

    if args.list_voices:
        print(run_ps(PS_LIST))
        return

    RATE = max(8000, min(16000, args.rate_hz))
    vocab = read_vocab()
    os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        jobs = []
        sources = {}
        for clip, text in vocab:
            custom = os.path.join(CUSTOM_DIR, clip + ".wav")
            if os.path.exists(custom):
                sources[clip] = custom
            else:
                path = os.path.join(tmp, clip + ".wav")
                jobs.append({"path": path, "text": text})
                sources[clip] = path
        if jobs:
            job_file = os.path.join(tmp, "jobs.json")
            with open(job_file, "w", encoding="utf-8") as f:
                json.dump(jobs, f)
            print(f"Synthesising {len(jobs)} clips with SAPI {args.voice or '(default voice)'}...")
            run_ps(PS_SYNTH, "-JobFile", job_file, "-Voice", args.voice, "-Rate", str(args.rate))

        total = 0
        pack = bytearray(b"PVP1")
        for clip, _ in vocab:
            if len(clip.encode()) >= 20:
                sys.exit(f"clip name {clip!r} is too long for the pack (19 characters at most)")
            samples = trim_and_normalise(read_wav(sources[clip]))
            data = b"IMA1" + struct.pack("<II", RATE, len(samples)) + adpcm_encode(samples)
            pack += clip.encode().ljust(20, b"\0") + struct.pack("<I", len(data)) + data
            total += len(data)
        with open(OUT_FILE, "wb") as f:
            f.write(pack)

    if os.path.isdir(OLD_DIR):  # one file per clip is the old layout: it must not be uploaded next to the pack
        for name in os.listdir(OLD_DIR):
            os.remove(os.path.join(OLD_DIR, name))
        os.rmdir(OLD_DIR)
    print(f"Wrote {len(vocab)} clips ({total / 1024:.0f} kB) to {os.path.relpath(OUT_FILE, ROOT)} ({len(pack) / 1024:.0f} kB)")


if __name__ == "__main__":
    main()
