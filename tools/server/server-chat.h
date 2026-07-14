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

// ---------------------------------------------------------------------------
// Unified tool normalizer for Responses bridge
// Converts model output in any known XML/JSON format into normalized tool calls
// and resolves them against the responses_tool_map.
// ---------------------------------------------------------------------------

struct NormalizedToolCall {
    std::string name;           // raw name from source
    std::string arguments;      // JSON string
    std::string namespace_name; // namespace if resolved from mapping
    std::string source_format;  // "tool_call", "invoke", "tool_search", "json"
    bool partial = false;       // incomplete during streaming
};

// Resolve a raw tool name against the tool_map following strict priority:
//   1. exact map key
//   2. sanitized key
//   3. namespace + original_name
//   4. unique original_name  (reject if ambiguous)
// Returns empty string on no match or ambiguity.
std::string resolve_tool_name(
    const std::string & name,
    const std::string & namespace_name,
    const std::map<std::string, nlohmann::ordered_json> & tool_map);

// Parse model output text into normalized tool calls, resolving names
// against the optional tool_map.  Formats supported:
//   <tool_call>name{json}</tool_call>
//   <tool_call>{name:..., arguments:...}</tool_call>
//   <invoke name=X><parameter name=k>v</parameter></invoke>
//   <tool_search>query text</tool_search>
// Unmatched or ambiguous calls are kept as content text.
void parse_normalized_tool_calls(
    const std::string & text,
    bool is_partial,
    const std::string & gen_prompt,
    const std::map<std::string, nlohmann::ordered_json> * tool_map,
    std::vector<NormalizedToolCall> & out_calls,
    std::string & clean_content);

// Convert a vector of NormalizedToolCall into a common_chat_msg
// (populates msg.content, msg.tool_calls).
void normalized_calls_to_chat_msg(
    common_chat_msg & msg,
    const std::vector<NormalizedToolCall> & calls);

// Parse <tool_call>name{json}</tool_call> format from generated text without invoking
// the full PEG tool-call grammar (used when tools are not injected into model prompt).
// Returns true if any tool calls were detected and msg was updated.
bool parse_xml_tool_call_fallback(
    const std::string & raw_text,
    bool is_partial,
    const std::string & gen_prompt,
    common_chat_msg & msg,
    const std::map<std::string, nlohmann::ordered_json> * tool_map = nullptr);

// Convert Anthropic Messages API format to OpenAI Chat Completions API format
json server_chat_convert_anthropic_to_oai(const json & body);

// convert OpenAI transcriptions API format to OpenAI Chat Completions API format
json convert_transcriptions_to_chatcmpl(
    const json & body,
    const common_chat_templates * tmpls,
    const std::map<std::string, uploaded_file> & in_files,
    std::vector<raw_buffer> & out_files);

json server_chat_msg_diff_to_json_oaicompat(const common_chat_msg_diff & diff);
