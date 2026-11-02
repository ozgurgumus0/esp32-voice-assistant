# Dinozaushka: an ESP32-S3 voice assistant with a dinosaur personality

A standalone, cloud-powered voice assistant on a **Freenove ESP32-S3 2.8" CYD**
(FNK0104AB, a "Cheap Yellow Display" board with a touchscreen, mic and speaker).
Tap the dinosaur's head, talk, and it answers out loud, with an animated face
that changes expression to match its mood.

Its persona is **Dinozaushka** ("like *babushka*, but dinosaur"): a sassy
Spinosaurus who never lets you forget it was the biggest carnivorous dinosaur
that ever lived. It remembers each of its two owners separately, tells them
apart by voice pitch alone, and can search the web when a question needs fresh
information.

## How it works

```
tap ─► record (I2S mic, ES8311 codec)
     ─► speaker guess (pitch estimate on the raw audio)
     ─► Whisper speech-to-text
     ─► LLM reply (OpenAI Responses API + hosted web_search, or DeepSeek)
          with [emotion] / [remember: …] / [forget: …] tags
     ─► OpenAI text-to-speech (gpt-4o-mini-tts, "dinosaur" voice style)
     ─► I2S playback while the face animates
```

Everything runs on the board: no phone app and no server in between.

## Features

- **Animated character UI.** A dinosaur with a body, idle bobbing, blinking
  and five emotions (happy, sad, thinking, surprised, confused), drawn with
  hand-coded shapes on TFT_eSPI (no LVGL).
- **Per-speaker memory.** There are two profiles, each with a rolling history
  (40 entries) and up to 30 long-term facts, saved to LittleFS so they survive
  power cycles. The model saves facts with an inline `[remember: …]` tag, and
  `[forget: …]` / `[forget_all]` actually delete them rather than just claiming to.
- **Speaker detection by pitch.** A normalized-autocorrelation F0 estimate
  searches from the lowest frequency up and commits to the first confidently
  periodic lag. This avoids the classic mistake of locking onto the 2nd
  harmonic of a low voice. No login, and no need to say who you are.
- **Web search.** The OpenAI branch uses the Responses API with the hosted
  `web_search` tool, and the model decides per turn whether to search. A
  markdown-stripping safety net keeps links and `**bold**` out of the speech.
- **On-device WiFi setup.** A touchscreen network list with an on-screen
  keyboard, plus a captive-portal fallback ("Dino Setup" access point) for
  hidden networks.
- **TLS done properly.** Root CAs are pinned in `include/certs.h`, with no
  `setInsecure()`, so API keys and audio can't be read on a hostile network.

## Hardware

| Part | Notes |
|---|---|
| Freenove ESP32-S3 2.8" CYD (FNK0104AB) | 16 MB flash, 8 MB octal PSRAM, ILI9341 240×320 display |
| FT6336U capacitive touch | Vendored in `lib/FT6336U` (MIT), patched to bound its I2C retry loop |
| ES8311 audio codec | Vendored in `lib/es8311` (Apache-2.0, Espressif) |
| Onboard mic and speaker | 24 kHz mono, which matches the OpenAI TTS output rate |

## Setup

1. Install [PlatformIO](https://platformio.org/). The project uses the
   [pioarduino](https://github.com/pioarduino/platform-espressif32) platform
   fork, because the I2S API needs Arduino-ESP32 3.x.
2. Copy `include/secrets.example.h` to `include/secrets.h` and add your
   OpenAI API key. `secrets.h` is gitignored.
3. Optionally, edit `include/config.h` to change the persona, voice, model,
   speaker names and the pitch threshold (`VOICE_PITCH_THRESHOLD_HZ`).
4. Build and flash:
   ```
   pio run -e freenove-cyd-s3 -t upload
   ```
   On Windows, run this from PowerShell rather than Git Bash, because the
   toolchain setup breaks under MSYS.
5. On first boot, pick your WiFi network on the touchscreen, then tap the
   dinosaur and talk.

## Project layout

```
include/   config.h (all tunables), pins.h, certs.h, secrets.example.h
src/       main loop, display, touch, audio, profiles (memory + speaker ID),
           net_stt / net_llm / net_tts / net_http, wifi_setup
lib/       vendored FT6336U touch and ES8311 codec drivers
```

## Lessons learned

- **The small model tier needs different parameters.** The fast GPT tier
  rejects `max_tokens` and any non-default `temperature`. It also spends
  hidden reasoning tokens, so a tight output cap fails with an HTTP 400 rather
  than just truncating the reply.
- **Don't trust tag order.** The model doesn't always emit tags in the order
  you asked for, so the parser strips leading `[…]` tags one at a time and
  dispatches each by its content.
- **Prompt rules can break when tools fire.** Length and no-markdown rules
  were ignored when web search ran, so there's now a code-level safety net
  as well as the prompt instruction.
- **`sizeof` on a PSRAM buffer pointer** returns 4 bytes, not the buffer's
  capacity, so the capacity is tracked explicitly.
- **A serial-monitor reconnect resets this board** (`USB_UART_CHIP_RESET`).
  Look for the full ROM boot banner before concluding it was a real power cycle.
