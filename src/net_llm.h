#pragma once
#include <Arduino.h>

// Sends userText (plus a short rolling conversation history) to the LLM
// provider configured in config.h (OpenAI or DeepSeek -- same request/response
// schema, only base URL/model differ). On success fills replyText (the
// spoken reply with the leading emotion tag already stripped) and emotionTag
// (one of "happy"/"sad"/"thinking"/"surprised"/"confused", or "" if the model
// didn't include a recognisable one).
bool llm_chat(const String &userText, String &replyText, String &emotionTag);
