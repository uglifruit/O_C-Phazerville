# WAVRecorderApplet — Notes

**File:** `software/src/audio_applets/WAVRecorderApplet.h`
**Registered in:** `software/src/hemisphere_audio_config.h` — both `mono_processors_pool` and `stereo_processors_pool`
**Slots:** 1–4 (processor chain only — not a source)

---

## What it does

Records live audio to the SD card as a standard 16-bit PCM `.WAV` file while sitting transparently in the signal chain. Audio passes through unmodified at zero copy cost; the record queues are side-channel sinks tapped from the same passthrough node.

Files are named `R001.WAV` … `R999.WAV` in the SD root. In stereo mode, one interleaved 2-channel file is written rather than two mono files.

---

## Signal flow

```
                         ┌──► AudioRecordQueue[0] ──┐
prev_slot ──► passthru ──┤                           ├──► PSRAM ring ──► SD file
                         └──► AudioRecordQueue[1] ──┘    (stereo only)
                              (STEREO only)

passthru ──► next_slot   (zero-copy; same object used for InputStream and OutputStream)
```

`AudioRecordQueue` objects are patched in `Start()` via `PatchCable`. They are enabled (`begin()`) only when recording starts and disabled (`end()`) when it stops, so they produce no overhead at idle.

---

## PSRAM ring buffer

**Size:** 1 MB (`RING_BYTES = 1024 * 1024`)

SD card wear-levelling can stall the bus for up to ~500 ms. At stereo 44100 Hz the maximum stall consumes ~88 KB, giving the 1 MB ring ~11× headroom. The ring is allocated from external PSRAM via `extmem_calloc` in `Start()` and freed in `Unload()`. If allocation fails (no PSRAM fitted), `View()` shows `No PSRAM` and recording is refused.

The ring is a classic head/tail circular buffer with wrap-around handled in `RingWrite()` and `FlushRingToSD()` using two `memcpy` calls when a write spans the end of the array.

**SD write chunk:** 4 KB (`SD_WRITE_CHUNK = 4096`). `FlushRingToSD(false)` only writes when ≥ 4 KB is available; `FlushRingToSD(true)` flushes everything (used at stop).

---

## WAV header

A 44-byte RIFF/PCM header is written as a placeholder when the file opens (size fields set to 0). On stop, `PatchWAVHeader()` seeks to offsets 4 and 40 to write the actual `riff_size` and `data_size` before closing.

If power is cut during recording those fields remain 0. The sample data in the file body is intact and can be recovered by hand-patching the header or with `ffmpeg -f s16le`.

---

## State machine

```
IDLE ──(trigger)──► STARTING ──(DoOpen OK)──► RECORDING
                        │                          │
                   (lock busy /             (trigger / Unload /
                    no PSRAM)                OnDataRequest /
                        │                    OnDataReceive)
                        ▼                          │
                      IDLE ◄──────── STOPPING ◄────┘
```

- **STARTING:** `mainloop()` calls `DoOpen()` — scans for next filename, opens file, writes WAV header placeholder, enables queues.
- **RECORDING:** `mainloop()` drains queues to ring, flushes 4 KB chunks to SD.
- **STOPPING:** `mainloop()` drains remaining queue data, flushes ring completely, calls `FinalizeWAV()`.
- Trigger (`arm_toggle`) is set in `Controller()` or `AuxButton()` and consumed in `mainloop()` to keep SD I/O off the ISR.

---

## SD lock

`inline bool wav_recorder_sd_lock` is a C++17 inline namespace-scope variable — shared across all template specialisations (`<MONO>` left, `<MONO>` right, `<STEREO>`). If one instance is recording, any other instance attempting to start will silently stay `IDLE` and show `LOCKED` in its display.

---

## Stereo interleaving

`DrainQueuesToRing()` only drains when **both** queues have data simultaneously (lock-step), preventing L/R sample drift. Blocks are interleaved into a 512-byte stack scratch buffer (`int16_t scratch[AUDIO_BLOCK_SAMPLES * 2]`) using `scratch[i * Channels + ch]` indexing — this expression is also correct for mono (Channels=1), producing a flat array with no branching.

---

## Savestate safety

Both `OnDataRequest()` and `OnDataReceive()` call `SyncStop()` before acting. `SyncStop()` performs the full drain-flush-finalize sequence synchronously (it is safe to call from main-loop context). This ensures:
- **Save:** file is closed and header is patched before the SD card handles the preset write.
- **Load:** file is closed before the trigger CV mapping is overwritten.

`Unload()` also calls `SyncStop()`, so swapping applets mid-recording is clean.

---

## Filename scanning

The SD root is scanned for `R001.WAV` … `R999.WAV` once on the first `mainloop()` call after load. The next available number is cached; `next_file_num` is incremented after each successful recording. If `SD.open()` fails unexpectedly (SD swapped, card full), the applet returns to IDLE and the lock is released.

---

## Display layout

```
┌────────────────────────────────┐
│  (framework header)            │  y 0–13
│  IDLE  [CLK1]                  │  y 15  — state (inverted when REC) + trigger CV
│  R042.WAV                      │  y 25  — next/current filename
│  00:03.271   or  AUX/CV:       │  y 35  — timer (recording) / hint (idle)
│              or  rec/stop      │  y 44  — hint line 2 / OVERFLOW! / LOCKED
│                                │
│  ████████████░░░░░░░░░░░░░░░   │  y 54  — VU peak bar (recording only)
└────────────────────────────────┘
```

The applet name in the selector header reads `WavRec*` (blinking `*`) while recording.

---

## Known limitations / future work

- **Power-off mid-recording:** WAV header size fields are 0; data is recoverable by hand.
- **FAT32 4 GB file limit:** ~6.5 hours stereo. No cap enforced; unlikely to matter in practice.
- **No pre-roll:** cannot capture audio before the trigger. Would need a second read-head on the ring buffer.
- **Filename scan on load:** files added externally while the applet is loaded will be missed until the applet is reloaded. Mitigation: re-scan on `SD.open()` failure.
