#include "net_llm.h"
#include "net_http.h"
#include "config.h"
#include "profiles.h"
#include <ArduinoJson.h>
#include <cctype>

// Conversation history and remembered facts now live in profiles.cpp, keyed
// by whichever of the two known speakers profiles_classify_speaker() thinks
// is talking -- persisted to flash there, not held in RAM here.

static String buildSystemPrompt() {
    return String(ASSISTANT_PERSONA) + " " + profiles_build_prompt_context() +
           " Start every reply with exactly one emotion tag describing it: "
           "[neutral], [happy], [sad], [thinking], [surprised] or [confused]. "
           "Example: \"[happy] Great question!\". The conversation history "
           "you're shown only covers the last stretch of chat -- it will NOT "
           "still contain something from hours or days ago unless you save "
           "it. So: DEFAULT TO SAVING anything with real content -- an "
           "opinion they gave, a personal detail, a project, plans, work/job "
           "stuff, a story, a question they asked and the real answer you "
           "gave, anything they'd reasonably expect you to still know later. "
           "Skip it only for pure filler with nothing to hold onto -- "
           "greetings, insults with no content, one-word replies, jokes. "
           "When genuinely unsure, save it anyway; a stale fact costs "
           "nothing, a lost one annoys them. To save something, add a "
           "second tag right after the first: [remember: short fact here]. "
           "Never repeat a fact you already know. Separately: when you DO "
           "bring up something you remember in a reply, it must actually "
           "connect to what they just said -- don't drop an old topic in "
           "out of nowhere just because you can, and be extra careful with "
           "anything that stings (rejections, failures, insecurities) -- "
           "only revisit that if the current moment genuinely calls for it, "
           "never as a random gotcha. If (and only if) they explicitly ask "
           "you to forget something, quote it back as closely as possible "
           "to how it reads in the \"Some things you already know about "
           "them\" list above, in a tag: [forget: that fact, quoted closely]. "
           "If they ask you to forget everything / wipe your memory of "
           "them, use [forget_all] instead (no colon, no content) -- never "
           "use either forget tag unless they actually asked. If you have "
           "live web search available and use it for something time-"
           "sensitive (news, weather, scores, prices, current events), the "
           "normal spoken-reply rules above STILL apply -- especially the "
           "one about length. Searching is not an excuse to write a report: "
           "pick the one best answer and say it in the same ~1 short "
           "sentence (up to 3-5 for something that genuinely needs it) as "
           "everything else, never multiple paragraphs comparing sources or "
           "hedging between candidates. This is spoken text-to-speech, not "
           "a page you're rendering, so it must be 100% plain words only: "
           "NO markdown of any kind (no **bold**, no [links](url), no bullet "
           "lists), NO literal URLs, and NO \"according to X\" / \"source: "
           "Y\" citations -- if a name like Wikipedia is worth mentioning at "
           "all, just say the plain word \"Wikipedia\" in the sentence, "
           "nothing bracketed or linked. After the tag(s), write only the "
           "text to be spoken aloud, with no other explanation.";
}

// Web-search replies especially tend to carry markdown links/bold/bare URLs
// even when told not to (seen in testing despite an explicit instruction) --
// this is spoken text-to-speech, so strip that stuff here as a safety net
// rather than trusting the prompt alone to hold every time.
static String stripMarkdownForSpeech(const String &textIn) {
    String text = textIn;

    // [label](url) -> label
    int searchFrom = 0;
    for (;;) {
        int openBracket = text.indexOf('[', searchFrom);
        if (openBracket < 0) break;
        int closeBracket = text.indexOf(']', openBracket);
        if (closeBracket < 0) break;
        if (closeBracket + 1 < (int)text.length() && text[closeBracket + 1] == '(') {
            int closeParen = text.indexOf(')', closeBracket);
            if (closeParen < 0) {
                searchFrom = openBracket + 1;
                continue;
            }
            String label = text.substring(openBracket + 1, closeBracket);
            text = text.substring(0, openBracket) + label + text.substring(closeParen + 1);
            searchFrom = openBracket + label.length();
        } else {
            searchFrom = closeBracket + 1;
        }
    }

    // Bare "https://..."/"http://..." with no markdown wrapper.
    const char *urlPrefixes[] = {"https://", "http://"};
    for (const char *prefix : urlPrefixes) {
        int pos;
        while ((pos = text.indexOf(prefix)) >= 0) {
            int end = pos;
            while (end < (int)text.length() && !isspace((unsigned char)text[end]) && text[end] != ')') end++;
            text = text.substring(0, pos) + text.substring(end);
        }
    }

    text.replace("**", "");
    text.replace("__", "");
    text.trim();
    return text;
}

#if LLM_PROVIDER != LLM_PROVIDER_DEEPSEEK
// Pulls the first {"type":"message", content:[{"type":"output_text","text":...}]}
// item out of a Responses API "output" array -- the array also contains
// "reasoning" and (when it searched) "web_search_call" items we don't need.
static String extractResponsesText(JsonDocument &respDoc) {
    for (JsonObject item : respDoc["output"].as<JsonArray>()) {
        const char *type = item["type"] | "";
        if (strcmp(type, "message") != 0) continue;
        for (JsonObject c : item["content"].as<JsonArray>()) {
            const char *ctype = c["type"] | "";
            if (strcmp(ctype, "output_text") == 0) {
                return c["text"].as<String>();
            }
        }
    }
    return "";
}
#endif

bool llm_chat(const String &userText, String &replyText, String &emotionTag) {
    JsonDocument reqDoc;
    reqDoc["model"] = LLM_MODEL;

    HistEntry history[PROFILE_HISTORY_MAX];
    int historyCount = profiles_get_history(history, PROFILE_HISTORY_MAX);
    Serial.printf("[llm] history has %d prior turn(s)\n", historyCount);

#if LLM_PROVIDER == LLM_PROVIDER_DEEPSEEK
    reqDoc["max_tokens"] = LLM_MAX_TOKENS;
    reqDoc["temperature"] = LLM_TEMPERATURE;

    JsonArray messages = reqDoc["messages"].to<JsonArray>();
    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = buildSystemPrompt();
    for (int i = 0; i < historyCount; i++) {
        Serial.printf("[llm]   history[%d] %s: %s\n", i, history[i].role.c_str(), history[i].content.c_str());
        JsonObject m = messages.add<JsonObject>();
        m["role"] = history[i].role;
        m["content"] = history[i].content;
    }
    JsonObject userMsg = messages.add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = userText;

    const char *host = "api.deepseek.com";
    const char *path = "/chat/completions";
    const char *apiKey = DEEPSEEK_API_KEY;
#else
    // Responses API instead of Chat Completions -- this is what gives it
    // access to OpenAI's hosted "web_search" tool (confirmed working on the
    // gpt-5.6-luna tier). tool_choice defaults to "auto": the model decides
    // per-turn whether a query actually needs a live search, so ordinary
    // banter isn't slowed down by it. Newer tiers also want
    // "max_output_tokens" here, not "max_tokens"/"max_completion_tokens".
    reqDoc["max_output_tokens"] = LLM_MAX_TOKENS;
    JsonObject webSearchTool = reqDoc["tools"].to<JsonArray>().add<JsonObject>();
    webSearchTool["type"] = "web_search";

    JsonArray input = reqDoc["input"].to<JsonArray>();
    JsonObject sysMsg = input.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = buildSystemPrompt();
    for (int i = 0; i < historyCount; i++) {
        Serial.printf("[llm]   history[%d] %s: %s\n", i, history[i].role.c_str(), history[i].content.c_str());
        JsonObject m = input.add<JsonObject>();
        m["role"] = history[i].role;
        m["content"] = history[i].content;
    }
    JsonObject userMsg = input.add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = userText;

    const char *host = "api.openai.com";
    const char *path = "/v1/responses";
    const char *apiKey = OPENAI_API_KEY;
#endif

    String body;
    serializeJson(reqDoc, body);

#if LLM_PROVIDER == LLM_PROVIDER_DEEPSEEK
    static uint8_t respBuf[6144];
    const size_t respCapacity = sizeof(respBuf);
#else
    // Responses API replies carry reasoning items with large opaque
    // "encrypted_content" blobs (and web_search_call metadata when it
    // searches) that a plain Chat Completions reply never had -- a real
    // response here easily runs 8-15KB+ even though we only end up reading
    // a few hundred bytes of actual message text out of it. PSRAM-backed so
    // this doesn't have to fit the old fixed 6KB budget. (NOTE: respBuf is a
    // pointer here, not an array -- sizeof(respBuf) would silently give the
    // pointer's own size, not the buffer's, so capacity is tracked
    // separately rather than derived from it.)
    const size_t respCapacity = 49152;
    uint8_t *respBuf = (uint8_t *)ps_malloc(respCapacity);
    if (!respBuf) {
        Serial.println("[llm] failed to allocate PSRAM response buffer");
        return false;
    }
#endif
    HttpResult resp = http_post_text(host, path, apiKey, "application/json",
                                      (const uint8_t *)body.c_str(), body.length(),
                                      respBuf, respCapacity - 1);
    respBuf[min(resp.bodyLen, respCapacity - 1)] = 0;

    if (resp.statusCode != 200) {
        Serial.printf("[llm] HTTP %d: %s\n", resp.statusCode, (char *)respBuf);
#if LLM_PROVIDER != LLM_PROVIDER_DEEPSEEK
        free(respBuf);
#endif
        return false;
    }

    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, (char *)respBuf, resp.bodyLen);
#if LLM_PROVIDER != LLM_PROVIDER_DEEPSEEK
    free(respBuf);
#endif
    if (err) {
        Serial.print("[llm] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

#if LLM_PROVIDER == LLM_PROVIDER_DEEPSEEK
    String content = respDoc["choices"][0]["message"]["content"].as<String>();
#else
    String content = extractResponsesText(respDoc);
#endif
    content.trim();

    // The model is asked for an emotion tag first and optional remember/
    // forget tags after, but doesn't always comply with that order -- so
    // rather than assuming a fixed position, strip leading bracket tags one
    // at a time and dispatch each by its own content instead of by where it
    // appears.
    emotionTag = "";
    while (content.startsWith("[")) {
        int close = content.indexOf(']');
        if (close <= 0) break;
        String tag = content.substring(1, close);
        String rest = content.substring(close + 1);
        rest.trim();

        String tagLower = tag;
        tagLower.toLowerCase();
        if (tagLower.startsWith("remember:")) {
            String fact = tag.substring(9); // skip "remember:"
            fact.trim();
            profiles_remember_fact(fact);
        } else if (tagLower.startsWith("forget:")) {
            String fragment = tag.substring(7); // skip "forget:"
            fragment.trim();
            profiles_forget_fact(fragment);
        } else if (tagLower == "forget_all") {
            profiles_forget_all();
        } else {
            emotionTag = tagLower;
        }
        content = rest;
    }
    replyText = stripMarkdownForSpeech(content);

    if (replyText.length() > 0) {
        profiles_push_history("user", userText);
        profiles_push_history("assistant", replyText);
        profiles_save();
    }

    return replyText.length() > 0;
}
