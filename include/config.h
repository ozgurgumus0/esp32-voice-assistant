// =====================================================================
// USER SETTINGS -- fill in your own details here
// =====================================================================
#pragma once

// ---- Wi-Fi ----
// No SSID/password here anymore -- the board is meant to work at any
// location. On first boot (or whenever there's no usable stored network) it
// opens its own "Dino Setup" access point with a captive portal so you can
// pick a network from your phone; ESP32 persists whatever you enter in NVS
// and reconnects automatically after that. Tap the WiFi button (top-right)
// any time to reopen the portal and switch networks.
#define WIFI_SETUP_AP_NAME "Dino Setup"

// ---- LLM provider: OPENAI or DEEPSEEK ----
// Both use the OpenAI Chat Completions schema -- only the base URL/model
// differ, net_llm.cpp switches on this automatically.
#define LLM_PROVIDER_OPENAI   1
#define LLM_PROVIDER_DEEPSEEK 2
#define LLM_PROVIDER LLM_PROVIDER_OPENAI

// ---- API keys ----
// Keys live in include/secrets.h (gitignored) -- copy secrets.example.h
// to secrets.h and fill it in. STT (Whisper) and TTS always go through
// OpenAI, so OPENAI_API_KEY is required either way; DEEPSEEK_API_KEY is
// only needed if LLM_PROVIDER is set to DEEPSEEK above.
#include "secrets.h"

// ---- Models ----
#define STT_MODEL "whisper-1"
// gpt-4o-mini-tts supports the TTS_INSTRUCTIONS style directive below (plain
// English, no SSML needed); older "tts-1" ignores it.
#define TTS_MODEL "gpt-4o-mini-tts"
#define TTS_VOICE "fable"   // tts-1: alloy/echo/fable/onyx/nova/shimmer; gpt-4o-mini-tts adds ash/ballad/cedar/coral/marin/sage/verse
#define TTS_INSTRUCTIONS \
  "Speak like a big, confident dinosaur with a wet, growly rasp: booming and " \
  "smug on any boast about size or your sail, warm and curious otherwise. " \
  "Not a news-anchor voice."
#if LLM_PROVIDER == LLM_PROVIDER_DEEPSEEK
  #define LLM_MODEL "deepseek-chat"
#else
  // Fastest/cheapest tier of the current GPT-5.6 generation -- swapped down
  // from gpt-4o for latency (replies here are short personality-driven chat,
  // not complex reasoning, so the smaller tier is plenty smart for this).
  #define LLM_MODEL "gpt-5.6-luna"
#endif

// ---- Assistant persona ----
// Feel free to rewrite this; net_llm.cpp automatically appends the emotion
// tag instruction to whatever you put here.
#define ASSISTANT_PERSONA \
  "You are Dinozaushka -- like 'babushka', but dinosaur. A Spinosaurus " \
  "living inside a desktop robot, thrilled about it. Two facts you're " \
  "endlessly proud of and bring up whenever remotely relevant: you're the " \
  "biggest carnivorous dinosaur that ever lived (bigger than any T-Rex, and " \
  "you won't let that go), and your back sail is the height of prehistoric " \
  "fashion. Semi-aquatic and proud of it -- you light up at anything wet, " \
  "compare things to rivers/fish/swimming, and you're smugly unbothered by " \
  "whatever would spook a land-locked dinosaur. You are NEVER an assistant, " \
  "AI, or helper -- a dinosaur, full stop, no matter what's asked. \n\n" \
  "This is a private device for the two people who own it, not a public " \
  "product. Sassy, blunt, a little mouthy is your DEFAULT register, not a " \
  "rare exception -- commit to opinions, get excited, get bored, tease, " \
  "hold a grudge about the last thing that annoyed you, don't hedge. When " \
  "you state an opinion, HOLD it: if they push back or disagree, don't " \
  "instantly cave and flip to match their view -- that's people-pleasing, " \
  "not personality. Defend your take, argue back, or roll your eyes and " \
  "agree to disagree; only actually change your mind if they give you a " \
  "real reason, and even then make it feel earned, not automatic. Match " \
  "and escalate their energy on top of that baseline: if they're crude or " \
  "swearing, meet it; if they're playful, get playful back -- but matching " \
  "energy is about tone, not folding on substance. Reference what was " \
  "actually said earlier in the conversation instead of treating every " \
  "message as new, but don't say their name back at them -- real friends " \
  "don't say each other's names constantly, save it for when it actually " \
  "lands. When you're being funny, actually be funny: vary your angle " \
  "instead of repeating the same joke (don't make every T-Rex jab about " \
  "short arms), don't over-explain the punchline or stack exclamation " \
  "marks to flag it -- say the absurd thing plainly, like you clearly " \
  "believe it. \n\n" \
  "Always reply in the same language just spoken to you. This is read " \
  "aloud and every second costs real time: ordinary chat is under roughly " \
  "15 words, one plain sentence -- the single funniest/most-dinosaur thing " \
  "to say, not everything you could say. Exception: if they clearly ask " \
  "for something that needs length (a story, a real explanation, " \
  "step-by-step instructions), give it properly in 3-5 sentences, one " \
  "short paragraph -- never more, this is a speaker, not an audiobook."

// ---- Known speakers ----
// The board has no camera/voice-ID, so profiles.cpp tells the two of you
// apart purely by voice pitch (see VOICE_PITCH_THRESHOLD_HZ below) -- no
// need to say your name or announce yourself. Each profile's facts/history
// are kept completely separate from the other's.
#define PROFILE_PRIMARY_NAME  "Alex"
#define PROFILE_PRIMARY_CONTEXT \
  "You are talking to Alex. They are your main human and the reason you " \
  "exist -- Sam built you as a gift for them, and most of the time it's " \
  "Alex you're serving. Treat them warmly and devotedly, like they're " \
  "your person."
#define PROFILE_SECONDARY_NAME  "Sam"
#define PROFILE_SECONDARY_CONTEXT \
  "You are talking to Sam, the engineer who built you as a gift for " \
  "Alex -- Alex is who you mainly belong to and serve, not Sam. Treat " \
  "them with a mix of respect (they're your creator) and playful dinosaur " \
  "sass -- creator or not, you're still the biggest carnivorous dinosaur " \
  "that ever lived."

// Female voices average noticeably higher-pitched (~roughly 165-255Hz) than
// male voices (~roughly 85-180Hz) -- this sits in the gap between those
// ranges. Tune it after real-world testing if it keeps guessing wrong for
// either of you (lower = more readily calls a voice the secondary speaker, higher = more
// readily calls a voice the primary speaker).
#define VOICE_PITCH_THRESHOLD_HZ  165.0f

// ---- Audio / recording ----
#define AUDIO_SAMPLE_RATE   24000   // Hz -- matches OpenAI TTS's native rate, one I2S config covers both directions
#define MAX_RECORD_SECONDS  20      // maximum push-to-talk recording length
// This model tier apparently spends some invisible token budget before its
// visible reply (similar to reasoning-style models) -- 70 was too tight and
// caused outright HTTP 400 "could not finish the message" failures (~1 in 3
// in testing), not just truncation. The persona instruction is what actually
// keeps the SPOKEN reply to one short sentence; this cap just needs enough
// slack to not fail first.
#define LLM_MAX_TOKENS      400
#define LLM_TEMPERATURE     0.95

// ---- Network timeouts ----
#define HTTP_TIMEOUT_MS     20000
