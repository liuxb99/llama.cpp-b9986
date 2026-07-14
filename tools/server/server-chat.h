// Chat conversion functions for server (Responses API, Anthropic API, OAI streaming diffs)

#pragma once

#include "chat.h"
#include "server-common.h"
#include "server-http.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

using json = nlohmann::ordered_json;

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
