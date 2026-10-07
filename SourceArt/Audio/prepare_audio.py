"""Prepare supplied WAVs without modifying originals. Requires numpy; run from any directory."""
from pathlib import Path
import hashlib
import json
import struct
import wave
import numpy as np

ROOT = Path(__file__).resolve().parent
report = []
for src in sorted((ROOT / 'Package').glob('*.wav')):
    data = src.read_bytes()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError(src)
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        kind, size = struct.unpack_from('<4sI', data, pos)
        chunk = data[pos + 8:pos + 8 + size]
        if kind == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', chunk)
        if kind == b'data':
            pcm = chunk
        pos += 8 + size + size % 2
    tag, channels, rate, _, _, bits = fmt
    if (tag, bits) == (3, 32):
        samples = np.frombuffer(pcm, '<f4').astype(np.float64)
    elif (tag, bits) == (1, 16):
        samples = np.frombuffer(pcm, '<i2').astype(np.float64) / 32768
    else:
        raise ValueError((src, fmt))
    if not np.isfinite(samples).all():
        raise ValueError(f'Non-finite samples: {src}')
    samples = samples.reshape(-1, channels)
    peak = float(np.max(np.abs(samples)))
    gain = min(1., 10 ** (-1 / 20) / peak) if peak else 1.
    trim = 0
    # Trim only long leading silence of one-shots; leave compositions and all tails intact.
    if not src.stem.startswith(('BGM_', 'AMB_')) and peak:
        active = np.flatnonzero(np.max(np.abs(samples), axis=1) > .001)
        if len(active) and active[0] / rate > .1:
            trim = max(0, int(active[0]) - int(.025 * rate))
    out = ROOT / 'Prepared' / src.name
    out.parent.mkdir(exist_ok=True)
    with wave.open(str(out), 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(np.rint(samples[trim:] * gain * 32767).astype('<i2').tobytes())
    report.append(dict(name=src.name, source_sha256=hashlib.sha256(data).hexdigest(),
        prepared_sha256=hashlib.sha256(out.read_bytes()).hexdigest(), seconds=len(samples) / rate,
        channels=channels, rate=rate, source_peak=peak, gain=gain, trim_seconds=trim / rate,
        silent=peak == 0))
(ROOT / 'preparation_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(f'Prepared {len(report)} PCM WAV files; original sources unchanged.')
