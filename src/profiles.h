#pragma once
#include <Arduino.h>
#include "audio.h"

// Exactly two known speakers, told apart by voice pitch alone (no camera/
// voice-ID hardware on this board) -- see profiles_classify_speaker().
enum class ProfileId { PRIMARY, SECONDARY };

struct HistEntry {
    String role;
    String content;
};

// Even 10 turns proved too short to reliably survive to the next device
// close/reopen in practice -- 20 turns gives much more real-world coverage
// while staying cheap (extra input tokens on gpt-5.6-luna are negligible).
static const int PROFILE_HISTORY_MAX = 40;

// Mounts LittleFS (formatting it on first-ever boot) and loads any
// previously saved facts/history for both profiles from /profiles.json.
void profiles_init();

// Runs a coarse voice-pitch estimate over the just-recorded utterance and
// returns which of the two known speakers it more likely is. Falls back to
// whichever profile is already active if the recording is too short/quiet
// to get a confident pitch estimate, rather than guessing.
ProfileId profiles_classify_speaker(const AudioBuffer &wav);

void profiles_set_active(ProfileId id);
ProfileId profiles_get_active();

// The active profile's relationship framing (config.h) plus a compact digest
// of facts remembered about them so far -- appended into the system prompt.
String profiles_build_prompt_context();

// Copies up to maxCount of the active profile's persisted recent turns into
// out[], oldest first, returning how many were written.
int profiles_get_history(HistEntry *out, int maxCount);
void profiles_push_history(const String &role, const String &content);

// Stores a fact against the active profile (bounded list, oldest dropped).
// Tag parsing/extraction happens in net_llm.cpp, not here, so this is a
// plain primitive. Doesn't persist to flash by itself -- call profiles_save()
// once per turn after this and profiles_push_history().
void profiles_remember_fact(const String &fact);

// Deletes the first stored fact that case-insensitively matches (as a
// substring, either direction) `fragment`, for the active profile. A no-op
// if nothing matches -- doesn't persist by itself, see profiles_save().
void profiles_forget_fact(const String &fragment);

// Wipes ALL stored facts and rolling history for the active profile -- a
// full reset, for when they ask it to forget everything about them.
void profiles_forget_all();

// Writes the current in-memory state of both profiles to LittleFS.
void profiles_save();
