// Chat conversion functions for server (Responses API, Anthropic API, OAI streaming diffs)

#pragma once

#include "chat.h"
#include "server-common.h"
#include "server-http.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

using json = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Diagnostic helpers for Responses context/tool-bridge debugging
// Controlled by LLAMA_RESPONSES_CONTEXT_DEBUG=1 env var (default: off)
// All logging uses [RESP_CTX] prefix for easy filtering.
// ---------------------------------------------------------------------------

// Simple FNV-1a hash for diagnostic fingerprinting (no external deps)
static inline uint32_t resp_ctx_hash(const std::string & s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// Returns true once when LLAMA_RESPONSES_CONTEXT_DEBUG=1
static inline bool resp_ctx_debug_enabled() {
    static const bool enabled = []() {
        const char * env = std::getenv("LLAMA_RESPONSES_CONTEXT_DEBUG");
        return env && std::strcmp(env, "1") == 0;
    }();
    return enabled;
}

// Monotonic diagnostic sequence counter for correlating logs across stages
static inline uint32_t resp_ctx_next_seq() {
    static std::atomic<uint32_t> seq{0};
    return ++seq;
}

// Sanitize a tool/function name for use as a JSON key
std::string sanitize_tool_name(const std::string & name, const std::string & fallback = "tool");

// Convert OpenAI Responses API format to OpenAI Chat Completions API format
json server_chat_convert_responses_to_chatcmpl(const json & body);

// Build tool mapping from Responses tools for reverse lookup during output
json build_responses_tool_map(const json & response_body);

// Convert Anthropic Messages API format to OpenAI Chat Completions API format
json server_chat_convert_anthropic_to_oai(const json & body);

// convert OpenAI transcriptions API format to OpenAI Chat Completions API format
json convert_transcriptions_to_chatcmpl(
    const json & body,
    const common_chat_templates * tmpls,
    const std::map<std::string, uploaded_file> & in_files,
    std::vector<raw_buffer> & out_files);

json server_chat_msg_diff_to_json_oaicompat(const common_chat_msg_diff & diff);
