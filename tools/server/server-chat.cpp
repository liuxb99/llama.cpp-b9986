#include "server-chat.h"
#include "server-common.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

static bool exists_and_is_array(const json & j, const char * key) { return j.contains(key) && j.at(key).is_array(); }
static bool exists_and_is_string(const json & j, const char * key) { return j.contains(key) && j.at(key).is_string(); }

static std::string truncate_for_prompt(const std::string & text, size_t max_len = 64 * 1024) {
    return text.size() <= max_len ? text : text.substr(0, max_len) + "\n\n[truncated " + std::to_string(text.size() - max_len) + " bytes]";
}

static json responses_make_text_content(const std::string & text) {
    return json {
        {"text", text},
        {"type", "text"},
    };
}

std::string sanitize_tool_name(const std::string & name, const std::string & fallback) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char ch : name) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    if (out.empty()) {
        out = fallback;
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

static json parse_arguments_best_effort(const json & value) {
    if (value.is_object() || value.is_array()) {
        return value;
    }
    if (value.is_string()) {
        const std::string raw = value.get<std::string>();
        try {
            return json::parse(raw);
        } catch (const std::exception &) {
            return raw;
        }
    }
    if (value.is_null()) {
        return json::object();
    }
    return value;
}

static json make_tool_call(const std::string & name, const std::string & call_id, const json & arguments) {
    return json {
        {"function", json {
            {"arguments", arguments.is_string() ? arguments.get<std::string>() : arguments.dump()},
            {"name",      name},
        }},
        {"id",   call_id.empty() ? "call_" + random_string() : call_id},
        {"type", "function"},
    };
}

static void append_assistant_tool_call(std::vector<json> & messages, const json & tool_call) {
    if (!messages.empty() && messages.back().value("role", "") == "assistant") {
        auto & prev_msg = messages.back();
        if (!exists_and_is_array(prev_msg, "tool_calls")) {
            prev_msg["tool_calls"] = json::array();
        }
        prev_msg["tool_calls"].push_back(tool_call);
        return;
    }

    messages.push_back(json {
        {"role",       "assistant"},
        {"content",    json::array()},
        {"tool_calls", json::array({tool_call})},
    });
}

static std::string mcp_image_to_data_url(const json & image) {
    const std::string data = json_value(image, "data", std::string());
    if (data.empty()) {
        return "";
    }
    std::string mime_type = json_value(image, "mimeType", std::string());
    if (mime_type.empty()) {
        mime_type = json_value(image, "mime_type", std::string("image/png"));
    }
    return "data:" + mime_type + ";base64," + data;
}

static json encode_tool_output_content_item(const json & item) {
    const std::string type = json_value(item, "type", std::string());
    if (type == "input_text" || type == "output_text" || type == "text") {
        return responses_make_text_content(exists_and_is_string(item, "text")
            ? item.at("text").get<std::string>()
            : "[text output item missing text]");
    }
    if (type == "input_image") {
        if (exists_and_is_string(item, "image_url")) {
            return json {
                {"image_url", json {{"url", item.at("image_url")}}},
                {"type", "image_url"},
            };
        }
        return responses_make_text_content("[image output item missing image_url]");
    }
    if (type == "image") {
        const std::string data_url = mcp_image_to_data_url(item);
        if (!data_url.empty()) {
            return json {
                {"image_url", json {{"url", data_url}}},
                {"type", "image_url"},
            };
        }
        return responses_make_text_content("[image output item missing data]");
    }
    return responses_make_text_content("[unsupported tool output item: " + item.dump() + "]");
}

static json encode_tool_output_content(const json & output) {
    if (output.is_string()) {
        return output.get<std::string>();
    }
    if (output.is_array()) {
        json content = json::array();
        for (const auto & item : output) {
            if (item.is_object()) {
                content.push_back(encode_tool_output_content_item(item));
            } else {
                content.push_back(responses_make_text_content(item.is_string() ? item.get<std::string>() : item.is_null() ? "" : item.dump()));
            }
        }
        return content;
    }
    if (output.is_object()) {
        if (exists_and_is_array(output, "content")) {
            return encode_tool_output_content(output.at("content"));
        }
        if (exists_and_is_string(output, "body")) {
            return output.at("body").get<std::string>();
        }
        if (exists_and_is_string(output, "output")) {
            return output.at("output").get<std::string>();
        }
    }
    return output.dump();
}

static void append_tool_output_message(std::vector<json> & messages, const json & item) {
    const std::string call_id = json_value(item, "call_id", std::string());
    if (call_id.empty()) {
        messages.push_back(json {
            {"role", "assistant"},
            {"content", json::array({responses_make_text_content("[tool output missing call_id: " + item.dump() + "]")})},
        });
        return;
    }

    json content = json("[tool output missing output]");
    if (item.contains("output")) {
        content = encode_tool_output_content(item.at("output"));
    } else if (item.contains("tools")) {
        content = item.at("tools").dump();
    }
    messages.push_back(json {
        {"content",      content},
        {"role",         "tool"},
        {"tool_call_id", call_id},
    });
}

static std::string input_file_text(const json & input_item) {
    const std::string filename = json_value(input_item, "filename", std::string());
    const std::string file_id  = json_value(input_item, "file_id", std::string());
    const std::string file_url = json_value(input_item, "file_url", std::string());
    const std::string file_data = json_value(input_item, "file_data", std::string());
    const std::string label = filename.empty() ? (file_id.empty() ? "file" : file_id) : filename;

    if (!file_url.empty()) {
        return "[file: " + label + "] " + file_url;
    }
    if (!file_data.empty()) {
        if (file_data.rfind("data:image/", 0) == 0) {
            return "[image file: " + label + " omitted; use a vision-capable model to inspect image attachments]";
        }
        return "[file: " + label + " (inline data, " + std::to_string(file_data.size()) + " bytes)]";
    }
    return "[file: " + label + "]";
}

static std::string compaction_summary_text(const json & item) {
    if (!exists_and_is_array(item, "summary")) {
        return "";
    }
    std::string result;
    for (const auto & entry : item.at("summary")) {
        if (!result.empty()) {
            result += "\n";
        }
        if (entry.is_object() && exists_and_is_string(entry, "text")) {
            result += entry.at("text").get<std::string>();
        } else if (entry.is_string()) {
            result += entry.get<std::string>();
        }
    }
    return result;
}

static std::vector<json> responses_tool_to_chatcmpl_tools(const json & resp_tool) {
    std::vector<json> result;
    const std::string type = json_value(resp_tool, "type", std::string());

    if (type == "function") {
        json chatcmpl_tool;
        chatcmpl_tool["type"] = "function";
        json resp_function = resp_tool;

        if (resp_function.contains("name")) {
            resp_function["name"] = sanitize_tool_name(resp_function["name"].get<std::string>());
        }
        for (const char * key : {"type"}) {
            resp_function.erase(key);
        }
        if (!resp_function.contains("strict")) {
            resp_function["strict"] = true;
        }
        chatcmpl_tool["function"] = resp_function;
        result.push_back(chatcmpl_tool);

    } else if (type == "namespace") {
        const std::string ns_name = sanitize_tool_name(
            json_value(resp_tool, "name", std::string()), "namespace");
        if (resp_tool.contains("tools") && resp_tool.at("tools").is_array()) {
            for (const auto & sub : resp_tool.at("tools")) {
                if (json_value(sub, "type", std::string()) != "function") {
                    continue;
                }
                std::string sub_name = json_value(sub, "name", std::string());
                if (sub_name.empty()) {
                    continue;
                }
                // Prepend namespace name to avoid conflicts
                const std::string qualified_name = ns_name + "__" + sanitize_tool_name(sub_name);
                json fn = sub;
                fn["name"] = qualified_name;
                if (!fn.contains("strict")) {
                    fn["strict"] = false;
                }
                json chatcmpl_tool;
                chatcmpl_tool["type"] = "function";
                chatcmpl_tool["function"] = fn;
                result.push_back(chatcmpl_tool);
            }
        }

    } else if (type == "custom") {
        // Convert free-form custom tool to function with string input
        const std::string name = sanitize_tool_name(
            json_value(resp_tool, "name", std::string()), "custom_tool");
        const std::string desc = json_value(resp_tool, "description", std::string());
        json params = {
            {"type", "object"},
            {"properties", json{
                {"input", json{
                    {"type", "string"},
                    {"description", "Free-form input for the custom tool"},
                }},
            }},
            {"required", json::array({"input"})},
            {"additionalProperties", false},
        };
        json chatcmpl_tool;
        chatcmpl_tool["type"] = "function";
        chatcmpl_tool["function"] = json {
            {"name", name},
            {"description", desc},
            {"parameters", params},
            {"strict", false},
        };
        result.push_back(chatcmpl_tool);

    } else if (type == "tool_search") {
        // Convert tool_search to function with search query parameter
        const std::string desc = json_value(resp_tool, "description", std::string());
        json params = json_value(resp_tool, "parameters", json::object());
        if (params.empty()) {
            params = {
                {"type", "object"},
                {"properties", json{
                    {"query", json{
                        {"type", "string"},
                        {"description", "Search query to find relevant tools"},
                    }},
                }},
                {"required", json::array({"query"})},
                {"additionalProperties", false},
            };
        }
        json chatcmpl_tool;
        chatcmpl_tool["type"] = "function";
        chatcmpl_tool["function"] = json {
            {"name", "tool_search"},
            {"description", desc},
            {"parameters", params},
            {"strict", false},
        };
        result.push_back(chatcmpl_tool);

    } else if (type == "web_search") {
        // Convert web_search to internal function schema for model consumption
        json ws_params = {
            {"type", "object"},
            {"properties", json{
                {"query", json{{"type", "string"}, {"description", "Web search query to find information on the internet"}}},
            }},
            {"required", json::array({"query"})},
            {"additionalProperties", false},
        };
        json chatcmpl_tool;
        chatcmpl_tool["type"] = "function";
        chatcmpl_tool["function"] = json {
            {"name", "web_search"},
            {"description", "Search the web for real-time information"},
            {"parameters", ws_params},
            {"strict", false},
        };
        result.push_back(chatcmpl_tool);

    } else {
        SRV_WRN("unsupported Responses tool type '%s' skipped\n", type.c_str());
    }

    return result;
}

// Build a tool mapping from Responses tools array for reverse lookup during output.
// Each entry preserves the complete original tool definition for passthrough restoration.
// Key: exposed function name → {original_type, original_name, namespace_name, original_tool, ...}
json build_responses_tool_map(const json & response_body) {
    json map_obj = json::object();
    if (!response_body.contains("tools") || !response_body.at("tools").is_array()) {
        return map_obj;
    }
    for (const auto & tool : response_body.at("tools")) {
        const std::string type = json_value(tool, "type", std::string());
        if (type == "function") {
            const std::string name = sanitize_tool_name(
                json_value(tool, "name", std::string()));
            if (!name.empty()) {
                map_obj[name] = {
                    {"original_type", "function"},
                    {"original_name", json_value(tool, "name", std::string())},
                    {"original_tool", tool},
                };
            }
        } else if (type == "namespace") {
            const std::string ns_name = sanitize_tool_name(
                json_value(tool, "name", std::string()), "namespace");
            if (tool.contains("tools") && tool.at("tools").is_array()) {
                for (const auto & sub : tool.at("tools")) {
                    if (json_value(sub, "type", std::string()) != "function") {
                        continue;
                    }
                    std::string sub_name = json_value(sub, "name", std::string());
                    if (sub_name.empty()) { continue; }
                    const std::string qualified = ns_name + "__" + sanitize_tool_name(sub_name);
                    map_obj[qualified] = {
                        {"original_type", "namespace"},
                        {"original_name", sub_name},
                        {"namespace_name", json_value(tool, "name", std::string())},
                        {"original_tool", sub},
                    };
                }
            }
        } else if (type == "custom") {
            const std::string name = sanitize_tool_name(
                json_value(tool, "name", std::string()), "custom_tool");
            if (!name.empty()) {
                map_obj[name] = {
                    {"original_type", "custom"},
                    {"original_name", json_value(tool, "name", std::string())},
                    {"original_tool", tool},
                };
            }
        } else if (type == "web_search") {
            map_obj["web_search"] = {
                {"original_type", "web_search"},
                {"original_tool", tool},
            };
        } else if (type == "tool_search") {
            map_obj["tool_search"] = {
                {"original_type", "tool_search"},
                {"original_tool", tool},
                {"description", json_value(tool, "description", std::string())},
                {"execution", json_value(tool, "execution", std::string("sync"))},
            };
        }
    }
    return map_obj;
}

// Try to find a client search tool to use as web_search replacement.
// Returns the matching tool entry from chatcmpl_tools (or null if none found).
// When explicit_name is set, only match that exact function name.
static json find_web_search_replacement(
    const std::vector<json> & chatcmpl_tools,
    const std::string & explicit_name = "")
{
    for (const auto & tool : chatcmpl_tools) {
        if (!tool.is_object() || !tool.contains("function") || !tool.at("function").is_object()) {
            continue;
        }
        const auto & fn = tool.at("function");
        const std::string name = json_value(fn, "name", std::string());
        const std::string desc = json_value(fn, "description", std::string());
        if (name.empty()) { continue; }

        // Explicit name match has highest priority
        if (!explicit_name.empty()) {
            if (name == explicit_name) {
                return tool;
            }
            continue;
        }

        // Auto-matching: all comparisons are case-insensitive
        std::string name_lower = name;
        std::string desc_lower = desc;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        std::transform(desc_lower.begin(), desc_lower.end(), desc_lower.begin(), ::tolower);

        // Phase 1: Exact web-search names always match
        static const char * exact_web_names[] = {
            "web_search", "search_web", "websearch", "browser_search", nullptr,
        };
        for (const char ** p = exact_web_names; *p; p++) {
            if (name_lower == *p) { return tool; }
        }

        // Phase 2: Generic names (search, browser, fetch_url, fetch) ONLY match
        // if description clearly indicates web search
        bool is_generic = false;
        static const char * generic_search_names[] = {
            "search", "browser", "fetch_url", "fetch", nullptr,
        };
        for (const char ** p = generic_search_names; *p; p++) {
            if (name_lower == *p) { is_generic = true; break; }
        }

        if (is_generic) {
            // Description must mention both web/internet AND search/fetch/browse
            bool has_web = desc_lower.find("web") != std::string::npos ||
                           desc_lower.find("internet") != std::string::npos;
            bool has_search = desc_lower.find("search") != std::string::npos ||
                              desc_lower.find("fetch") != std::string::npos ||
                              desc_lower.find("browse") != std::string::npos;
            if (!has_web || !has_search) {
                continue;
            }
        } else {
            // Non-generic names: check description for web+search indicators
            if (!(desc_lower.find("web") != std::string::npos &&
                  desc_lower.find("search") != std::string::npos)) {
                continue;
            }
        }

        // Filter out non-web tools by description keywords (case-insensitive)
        static const char * exclude_keywords[] = {
            "code", "file", "repository", "grep", "tool search", "local", nullptr,
        };
        bool excluded = false;
        for (const char ** p = exclude_keywords; *p; p++) {
            if (desc_lower.find(*p) != std::string::npos) {
                excluded = true;
                break;
            }
        }
        if (excluded) {
            continue;
        }

        return tool;
    }
    return json();
}

bool parse_xml_tool_call_fallback(
    const std::string & raw_text,
    bool is_partial,
    const std::string & gen_prompt,
    common_chat_msg & msg) {
    std::string text = raw_text;
    if (!gen_prompt.empty() && text.substr(0, gen_prompt.size()) == gen_prompt) {
        text = text.substr(gen_prompt.size());
    }

    if (text.find("<tool_call>") == std::string::npos) {
        return false;
    }

    // Build clean content without <tool_call>...</tool_call> sections
    std::string clean_content;
    std::vector<common_chat_tool_call> calls;

    size_t pos = 0;

    while (true) {
        size_t ts = text.find("<tool_call>", pos);
        if (ts == std::string::npos) {
            clean_content += text.substr(pos);
            break;
        }

        // Add text before <tool_call>
        if (ts > pos) {
            clean_content += text.substr(pos, ts - pos);
        }

        size_t tag_end_start = text.find("</tool_call>", ts);

        std::string inner;
        if (tag_end_start != std::string::npos) {
            inner = text.substr(ts + 11, tag_end_start - ts - 11);
            pos = tag_end_start + 12;
        } else {
            inner = text.substr(ts + 11);
            pos = text.size();
        }

        // Trim whitespace
        size_t first = inner.find_first_not_of(" \t\n\r");
        if (first != std::string::npos) {
            inner = inner.substr(first);
        }
        size_t last = inner.find_last_not_of(" \t\n\r");
        if (last != std::string::npos && last + 1 < inner.size()) {
            inner = inner.substr(0, last + 1);
        }

        if (inner.empty()) {
            continue;
        }

        common_chat_tool_call call;
        bool parsed = false;

        // Format A: name{json}  or  name{json}name{json}  (parallel)
        if (std::isalpha(static_cast<unsigned char>(inner[0])) || inner[0] == '_') {
            size_t ci = 0;
            while (ci < inner.size()) {
                // Read name
                size_t name_start = ci;
                while (ci < inner.size() && (std::isalnum(static_cast<unsigned char>(inner[ci])) || inner[ci] == '_' || inner[ci] == '-' || inner[ci] == '.')) {
                    ci++;
                }
                if (ci == name_start) { ci++; continue; }
                common_chat_tool_call sub_call;
                sub_call.name = inner.substr(name_start, ci - name_start);

                // Skip whitespace before JSON
                while (ci < inner.size() && (inner[ci] == ' ' || inner[ci] == '\t' || inner[ci] == '\n' || inner[ci] == '\r')) {
                    ci++;
                }

                if (ci < inner.size() && inner[ci] == '{') {
                    int depth = 0;
                    size_t json_start = ci;
                    size_t json_end = std::string::npos;
                    for (; ci < inner.size(); ci++) {
                        if (inner[ci] == '{') depth++;
                        else if (inner[ci] == '}') {
                            depth--;
                            if (depth == 0) {
                                json_end = ci;
                                ci++;
                                break;
                            }
                        }
                    }
                    if (json_end != std::string::npos) {
                        sub_call.arguments = inner.substr(json_start, json_end - json_start + 1);
                    } else if (is_partial) {
                        sub_call.arguments = inner.substr(json_start);
                    }
                }

                if (!sub_call.name.empty()) {
                    if (!sub_call.arguments.empty()) {
                        auto parsed_json = json::parse(sub_call.arguments, nullptr, false);
                        if (!parsed_json.is_discarded() || is_partial) {
                            calls.push_back(std::move(sub_call));
                            parsed = true;
                        }
                    } else if (is_partial) {
                        calls.push_back(std::move(sub_call));
                        parsed = true;
                    }
                }
            }
        }

        // Format B: {"name":"...", "arguments":{...}}
        if (!parsed && !inner.empty() && inner[0] == '{') {
            auto parsed_json = json::parse(inner, nullptr, false);
            if (!parsed_json.is_discarded() && parsed_json.is_object()) {
                call.name = json_value(parsed_json, "name", std::string());
                if (!call.name.empty()) {
                    const json & args = parsed_json["arguments"];
                    call.arguments = args.dump();
                    calls.push_back(std::move(call));
                    parsed = true;
                }
            }
        }
    }

    msg.content = clean_content;
    if (!calls.empty()) {
        msg.tool_calls = std::move(calls);
        return true;
    }
    return false;
}

json server_chat_convert_responses_to_chatcmpl(const json & response_body) {
    if (!response_body.contains("input")) {
        throw std::invalid_argument("'input' is required");
    }
    if (!json_value(response_body, "previous_response_id", std::string{}).empty()) {
        SRV_WRN("%s", "Responses previous_response_id is accepted as replay-only state; callers must include prior input items\n");
    }

    json input_value = response_body.at("input");
    json chatcmpl_body = response_body;
    chatcmpl_body.erase("input");
    chatcmpl_body.erase("previous_response_id");
    std::vector<json> chatcmpl_messages;

    // [RESP_CTX] request_convert_start
    uint32_t resp_diag_seq = 0;
    if (resp_ctx_debug_enabled()) {
        resp_diag_seq = resp_ctx_next_seq();
        const char * input_type = input_value.is_string() ? "string" : (input_value.is_array() ? "array" : (input_value.is_object() ? "object" : "other"));
        size_t input_items = input_value.is_array() ? input_value.size() : 1;
        size_t instructions_chars = response_body.contains("instructions") ? response_body["instructions"].get<std::string>().size() : 0;
        size_t n_fn = 0, n_cust = 0, n_ns = 0, n_ns_sub = 0, n_ts = 0, n_ws = 0, n_unk = 0;
        if (response_body.contains("tools") && response_body["tools"].is_array()) {
            for (const auto & t : response_body["tools"]) {
                const std::string tt = json_value(t, "type", std::string());
                if (tt == "function")       { n_fn++; }
                else if (tt == "custom")    { n_cust++; }
                else if (tt == "namespace") { n_ns++; if (t.contains("tools") && t["tools"].is_array()) n_ns_sub += t["tools"].size(); }
                else if (tt == "tool_search") { n_ts++; }
                else if (tt == "web_search")  { n_ws++; }
                else                        { n_unk++; }
            }
        }
        std::string ws_mode = json_value(response_body, "__responses_web_search_mode", std::string("native"));
        size_t body_chars = response_body.dump().size();
        SRV_CNT("[RESP_CTX] stage=request_convert_start"
                " diag_seq=%u input_type=%s input_items=%zu instructions_chars=%zu"
                " tools=%zu fn=%zu cust=%zu ns=%zu ns_sub=%zu ts=%zu ws=%zu unk=%zu"
                " ws_mode=%s body_chars=%zu\n",
                resp_diag_seq, input_type, input_items, instructions_chars,
                n_fn + n_cust + n_ns + n_ts + n_ws + n_unk,
                n_fn, n_cust, n_ns, n_ns_sub, n_ts, n_ws, n_unk,
                ws_mode.c_str(), body_chars);
    }

    if (response_body.contains("instructions")) {
        chatcmpl_messages.push_back({
            {"role",    "system"},
            {"content", json_value(response_body, "instructions", std::string())},
        });
        chatcmpl_body.erase("instructions");
    }

    if (input_value.is_string()) {
        // #responses_create-input-text_input
        chatcmpl_messages.push_back({
            {"role",    "user"},
            {"content", input_value},
        });
    } else if (input_value.is_array()) {
        // #responses_create-input-input_item_list

        for (json item : input_value) {
            bool merge_prev = !chatcmpl_messages.empty() && chatcmpl_messages.back().value("role", "") == "assistant";

            if (!item.is_object()) {
                // Non-object input items are skipped.
                continue;
            }

            if (exists_and_is_string(item, "content")) {
                // #responses_create-input-input_item_list-input_message-content-text_input
                // Only "Input message" contains item["content"]::string
                // After converting item["content"]::string to item["content"]::array,
                // we can treat "Input message" as sum of "Item-Input message" and "Item-Output message"
                item["content"] = json::array({
                    json {
                        {"text", item.at("content")},
                        {"type", "input_text"}
                    }
                });
            }

            if (exists_and_is_array(item, "content") &&
                exists_and_is_string(item, "role") &&
                (item.at("role") == "user" ||
                    item.at("role") == "system" ||
                    item.at("role") == "developer")
            ) {
                // #responses_create-input-input_item_list-item-input_message
                std::vector<json> chatcmpl_content;

                for (const json & input_item : item.at("content")) {
                    const std::string type = json_value(input_item, "type", std::string());

                    if (type == "input_text") {
                        if (!input_item.contains("text")) {
                            continue;
                        }
                        chatcmpl_content.push_back({
                            {"text", input_item.at("text")},
                            {"type", "text"},
                        });
                    } else if (type == "input_image") {
                        // While `detail` is marked as required,
                        // it has default value("auto") and can be omitted.

                        if (!input_item.contains("image_url")) {
                            continue;
                        }
                        chatcmpl_content.push_back({
                            {"image_url", json {
                                {"url", input_item.at("image_url")}
                            }},
                            {"type", "image_url"},
                        });
                    } else if (type == "input_file") {
                        chatcmpl_content.push_back(responses_make_text_content(input_file_text(input_item)));
                    } else {
                        // Unknown or malformed content parts are skipped.
                    }
                }

                if (item.contains("type")) {
                    item.erase("type");
                }
                if (item.contains("status")) {
                    item.erase("status");
                }
                item["content"] = chatcmpl_content;

                // Merge developer role into first system message
                if (item.at("role") == "developer") {
                    auto sys_it = std::find_if(chatcmpl_messages.begin(), chatcmpl_messages.end(),
                        [](const json & m) { return m.value("role", "") == "system"; });
                    if (sys_it != chatcmpl_messages.end()) {
                        auto & sys_content = (*sys_it)["content"];
                        if (sys_content.is_string()) {
                            sys_content = json::array({json{{"text", sys_content}, {"type", "text"}}});
                        }
                        for (const auto & part : chatcmpl_content) {
                            sys_content.push_back(part);
                        }
                        continue;
                    }
                    item["role"] = "system";
                }
                chatcmpl_messages.push_back(item);
            } else if (exists_and_is_string(item, "role") &&
                item.at("role") == "assistant" &&
                exists_and_is_string(item, "type") &&
                item.at("type") == "message"
            ) {
                // #responses_create-input-input_item_list-item-output_message
                auto chatcmpl_content = json::array();

                // Handle both string content and array content
                if (item.contains("content") && item.at("content").is_string()) {
                    // String content - convert to text content part
                    chatcmpl_content.push_back({
                        {"text", item.at("content")},
                        {"type", "text"},
                    });
                } else if (exists_and_is_array(item, "content")) {
                    // Array content - process each item
                    for (const auto & output_text : item.at("content")) {
                        const std::string type = json_value(output_text, "type", std::string());
                        if (type == "output_text" || type == "input_text") {
                            // Accept both output_text and input_text (string content gets converted to input_text)
                            if (!exists_and_is_string(output_text, "text")) {
                                continue;
                            }
                            chatcmpl_content.push_back({
                                {"text", output_text.at("text")},
                                {"type", "text"},
                            });
                        } else if (type == "refusal") {
                            if (!exists_and_is_string(output_text, "refusal")) {
                                throw std::invalid_argument("'Refusal' requires 'refusal'");
                            }
                            chatcmpl_content.push_back(responses_make_text_content(
                                "[assistant refusal] " + output_text.at("refusal").get<std::string>()));
                        } else if (type == "output_image") {
                            if (!output_text.contains("image_url")) {
                                throw std::invalid_argument("'image_url' is required for output_image");
                            }
                            chatcmpl_content.push_back({
                                {"image_url", json {
                                    {"url", output_text.at("image_url")}
                                }},
                                {"type", "image_url"},
                            });
                        } else {
                            // Unknown or malformed assistant content parts are skipped.
                        }
                    }
                }

                if (merge_prev) {
                    auto & prev_msg = chatcmpl_messages.back();
                    if (!exists_and_is_array(prev_msg, "content")) {
                        prev_msg["content"] = json::array();
                    }
                    auto & prev_content = prev_msg["content"];
                    for (const auto & part : chatcmpl_content) {
                        prev_content.push_back(part);
                    }
                } else {
                    item.erase("status");
                    item.erase("type");
                    item["content"] = chatcmpl_content;
                    chatcmpl_messages.push_back(item);
                }
            } else if (exists_and_is_string(item, "arguments") &&
                exists_and_is_string(item, "call_id") &&
                exists_and_is_string(item, "name") &&
                exists_and_is_string(item, "type") &&
                item.at("type") == "function_call"
            ) {
                // #responses_create-input-input_item_list-item-function_tool_call
                append_assistant_tool_call(chatcmpl_messages, make_tool_call(
                    item.at("name").get<std::string>(),
                    item.at("call_id").get<std::string>(),
                    parse_arguments_best_effort(item.at("arguments"))));
            } else if (exists_and_is_string(item, "type") &&
                (item.at("type") == "function_call" ||
                 item.at("type") == "custom_tool_call" ||
                 item.at("type") == "local_shell_call" ||
                 item.at("type") == "tool_search_call" ||
                 item.at("type") == "web_search_call" ||
                 item.at("type") == "file_search_call" ||
                 item.at("type") == "image_generation_call")
            ) {
                // #responses_create-input-input_item_list-item-other_tool_call
                std::string name = json_value(item, "name", std::string());
                if (name.empty()) {
                    name = "function";
                }
                json arguments;
                if (item.contains("arguments")) {
                    arguments = parse_arguments_best_effort(item.at("arguments"));
                } else if (item.contains("input")) {
                    arguments = json {{"input", json_value(item, "input", std::string())}};
                } else if (item.contains("action")) {
                    arguments = item.at("action");
                } else {
                    arguments = json::object();
                }
                append_assistant_tool_call(chatcmpl_messages, make_tool_call(
                    name,
                    json_value(item, "call_id", json_value(item, "id", std::string())),
                    arguments));
            } else if (exists_and_is_string(item, "call_id") &&
                (exists_and_is_string(item, "output") || exists_and_is_array(item, "output")) &&
                exists_and_is_string(item, "type") &&
                (item.at("type") == "function_call_output" || item.at("type") == "custom_tool_call_output" ||
                 item.at("type") == "mcp_tool_call_output" || item.at("type") == "web_search_output" ||
                 item.at("type") == "file_search_output"   || item.at("type") == "tool_search_output")
            ) {
                // #responses_create-input-input_item_list-item-function_tool_call_output
                append_tool_output_message(chatcmpl_messages, item);
            } else if (exists_and_is_array(item, "summary") &&
                exists_and_is_string(item, "type") &&
                item.at("type") == "reasoning") {
                // #responses_create-input-input_item_list-item-reasoning
                std::string reasoning_text;
                if (item.contains("content") && !item.at("content").is_null()) {
                    if (item.at("content").is_string()) {
                        reasoning_text = item.at("content").get<std::string>();
                    } else if (item.at("content").is_array() && !item.at("content").empty()
                               && exists_and_is_string(item.at("content")[0], "text")) {
                        reasoning_text = item.at("content")[0].at("text").get<std::string>();
                    }
                }

                if (merge_prev) {
                    auto & prev_msg = chatcmpl_messages.back();
                    prev_msg["reasoning_content"] = reasoning_text;
                } else {
                    chatcmpl_messages.push_back(json {
                        {"role", "assistant"},
                        {"content", json::array()},
                        {"reasoning_content", reasoning_text},
                    });
                }
            } else if (exists_and_is_string(item, "type") &&
                (item.at("type") == "compaction" || item.at("type") == "compaction_summary")) {
                const std::string summary = compaction_summary_text(item);
                if (!summary.empty()) {
                    chatcmpl_messages.push_back(json {
                        {"role", "user"},
                        {"content", "Previous conversation summary:\n\n" + summary},
                    });
                }
            } else {
                // Unknown or IDE-side items (e.g. ghost_snapshot) are skipped.
            }
        }
    } else if (input_value.is_object()) {
        // Single object input - wrap in array
        json item = input_value;
        bool merge_prev = !chatcmpl_messages.empty() && chatcmpl_messages.back().value("role", "") == "assistant";
        // Process as a single-item array
        if (exists_and_is_string(item, "content")) {
            item["content"] = json::array({
                json {{"text", item.at("content")}, {"type", "input_text"}}
            });
        }
        if (exists_and_is_array(item, "content") &&
            exists_and_is_string(item, "role") &&
            (item.at("role") == "user" || item.at("role") == "system" || item.at("role") == "developer")) {
            chatcmpl_messages.push_back(json {
                {"role", item.at("role")},
                {"content", item.at("content")},
            });
        } else {
            chatcmpl_messages.push_back(item);
        }
    } else {
        throw std::invalid_argument("'input' must be a string or array of objects");
    }

    // [RESP_CTX] history_conversion
    if (resp_ctx_debug_enabled() && input_value.is_array()) {
        size_t hist_total = 0, fn_calls = 0, fn_outputs = 0, cust_calls = 0, cust_outputs = 0, ts_calls = 0, ws_calls = 0, unknown = 0, dup_call_ids = 0;
        std::set<std::string> seen_call_ids;
        for (const auto & hist_item : input_value) {
            if (!hist_item.is_object()) continue;
            std::string ht = json_value(hist_item, "type", std::string());
            if (ht == "function_call")            { fn_calls++; }
            else if (ht == "function_call_output") { fn_outputs++; }
            else if (ht == "custom_tool_call")    { cust_calls++; }
            else if (ht == "custom_tool_call_output") { cust_outputs++; }
            else if (ht == "tool_search_call")    { ts_calls++; }
            else if (ht == "web_search_call")     { ws_calls++; }
            else if (ht == "message" || ht == "reasoning") { /* counted in hist_total */ }
            else { unknown++; }
            hist_total++;
            std::string cid = json_value(hist_item, "call_id", std::string());
            if (!cid.empty()) {
                if (!seen_call_ids.insert(cid).second) dup_call_ids++;
            }
        }
        std::string hist_dump = input_value.dump();
        size_t hist_chars = hist_dump.size();
        uint32_t hist_hash = resp_ctx_hash(hist_dump);
        size_t conv_msgs = chatcmpl_messages.size();
        SRV_CNT("[RESP_CTX] stage=history_conversion"
                " diag_seq=%u input_items=%zu conv_msgs=%zu"
                " fn_calls=%zu fn_outputs=%zu cust_calls=%zu cust_outputs=%zu ts_calls=%zu ws_calls=%zu unknown=%zu"
                " hist_chars=%zu hist_hash=%08x dup_call_ids=%zu\n",
                resp_diag_seq, hist_total, conv_msgs,
                fn_calls, fn_outputs, cust_calls, cust_outputs, ts_calls, ws_calls, unknown,
                hist_chars, hist_hash, dup_call_ids);
    }

    chatcmpl_body["messages"] = chatcmpl_messages;

    // Build tool map only — no tools injected into model prompt
    if (response_body.contains("tools") && response_body.at("tools").is_array() && !response_body.at("tools").empty()) {
        json tool_map = build_responses_tool_map(response_body);
        if (!tool_map.empty()) {
            chatcmpl_body["__responses_tool_map"] = tool_map;
        }
    }

    // Ensure tools are never passed to the model prompt
    chatcmpl_body.erase("tools");

    // Convert tool_choice object to Chat Completions format
    if (chatcmpl_body.contains("tool_choice") && chatcmpl_body.at("tool_choice").is_object()) {
        json choice = chatcmpl_body.at("tool_choice");
        const std::string choice_type = json_value(choice, "type", std::string());
        if (choice_type == "auto" || choice_type == "none" || choice_type == "required" || choice_type == "any") {
            chatcmpl_body["tool_choice"] = choice_type == "any" ? "required" : choice_type;
        } else {
            std::string name = json_value(choice, "name", std::string());
            for (const char * key : {"function", "tool"}) {
                if (name.empty() && choice.contains(key) && choice.at(key).is_object()) {
                    name = json_value(choice.at(key), "name", std::string());
                }
            }
            if (name.empty() && choice_type != "function") {
                name = choice_type.empty() ? "tool" : choice_type;
            }
            if (!name.empty() && chatcmpl_body.contains("tools") && chatcmpl_body.at("tools").is_array()) {
                const std::string selected_name = sanitize_tool_name(name, "tool");
                json selected_tools = json::array();
                for (const auto & tool : chatcmpl_body.at("tools")) {
                    if (tool.is_object() && tool.contains("function") && tool.at("function").is_object() &&
                        json_value(tool.at("function"), "name", std::string()) == selected_name) {
                        selected_tools.push_back(tool);
                    }
                }
                if (!selected_tools.empty()) {
                    chatcmpl_body["tools"] = selected_tools;
                }
            }
            chatcmpl_body["tool_choice"] = "required";
        }
    }

    // When no tools are injected into prompt, tool_choice is irrelevant
    if (!chatcmpl_body.contains("tools")) {
        chatcmpl_body.erase("tool_choice");
    }

    if (response_body.contains("max_output_tokens")) {
        chatcmpl_body.erase("max_output_tokens");
        chatcmpl_body["max_tokens"] = response_body["max_output_tokens"];
    }

    // Strip Responses-only keys that have no chat completions equivalent
    for (const char * key : {
        "store", "include", "prompt_cache_key", "web_search",
        "text", "truncation", "metadata", "reasoning", "background",
        "service_tier", "safety_identifier", "max_tool_calls",
    }) {
        chatcmpl_body.erase(key);
    }

    // [RESP_CTX] request_convert_end
    if (resp_ctx_debug_enabled()) {
        size_t n_msgs = chatcmpl_body.contains("messages") && chatcmpl_body["messages"].is_array() ? chatcmpl_body["messages"].size() : 0;
        size_t n_tools = chatcmpl_body.contains("tools") && chatcmpl_body["tools"].is_array() ? chatcmpl_body["tools"].size() : 0;
        size_t n_map = 0, n_orig_tool = 0;
        if (chatcmpl_body.contains("__responses_tool_map") && chatcmpl_body["__responses_tool_map"].is_object()) {
            n_map = chatcmpl_body["__responses_tool_map"].size();
            for (const auto & entry : chatcmpl_body["__responses_tool_map"].items()) {
                if (entry.value().is_object() && entry.value().contains("original_tool")) n_orig_tool++;
            }
        }
        size_t body_chars = chatcmpl_body.dump().size();
        int has_tools = chatcmpl_body.contains("tools") ? 1 : 0;
        int has_map  = chatcmpl_body.contains("__responses_tool_map") ? 1 : 0;
        SRV_CNT("[RESP_CTX] stage=request_convert_end"
                " diag_seq=%u messages=%zu tools=%zu tool_map=%zu original_tool=%zu body_chars=%zu has_tools=%d has_map=%d\n",
                resp_diag_seq, n_msgs, n_tools, n_map, n_orig_tool, body_chars, has_tools, has_map);
    }

    return chatcmpl_body;
}

// Edits the cch section of an "x-anthropic-billing-header" system prompt.
// Does nothing to any other prompt.
//
// This is a claude message with a "cch=ef01a" attribute that breaks prefix caching.
// The cch stamp is a whitebox end-to-end integrity hint. It's not meaningful as a
// system prompt data, particularly to llama.cpp, but its presence means the prefix
// cache will not get past it: It changes on each request.
//
// Reference: https://github.com/ggml-org/llama.cpp/pull/21793
// Example header:
// ```
// x-anthropic-billing-header: cc_version=2.1.101.e51; cc_entrypoint=cli; cch=a5145;You are Claude Code, Anthropic's official CLI for Claude.
//                                                                            ^^^^^
// ```
static void normalize_anthropic_billing_header(std::string & system_text) {
    if (system_text.rfind("x-anthropic-billing-header:", 0) != 0) {
        return;
    }

    const size_t header_prefix_length = strlen("x-anthropic-billing-header:");
    const size_t cch_length = 5;
    const size_t index_cch = system_text.find("cch=", header_prefix_length);
    if (index_cch == std::string::npos) {
        return;
    }

    const size_t index_replace = index_cch + 4;
    if (index_replace + cch_length < system_text.length() && system_text[index_replace + cch_length] == ';') {
        for (size_t i = 0; i < cch_length; ++i) {
            system_text[index_replace + i] = 'f';
        }
    } else {
        LOG_ERR("anthropic string not as expected: %s", system_text.c_str());
    }
}

json server_chat_convert_anthropic_to_oai(const json & body) {
    json oai_body;

    // Convert system prompt
    json oai_messages = json::array();
    auto system_param = json_value(body, "system", json());
    if (!system_param.is_null()) {
        std::string system_content;

        if (system_param.is_string()) {
            system_content = system_param.get<std::string>();
            normalize_anthropic_billing_header(system_content);
        } else if (system_param.is_array()) {
            for (const auto & block : system_param) {
                if (json_value(block, "type", std::string()) == "text") {
                    auto system_text = json_value(block, "text", std::string());
                    normalize_anthropic_billing_header(system_text);
                    system_content += system_text;
                }
            }
        }

        oai_messages.push_back({
            {"role", "system"},
            {"content", system_content}
        });
    }

    // Convert messages
    if (!body.contains("messages")) {
        throw std::runtime_error("'messages' is required");
    }
    const json & messages = body.at("messages");
    if (messages.is_array()) {
        for (const auto & msg : messages) {
            std::string role = json_value(msg, "role", std::string());

            if (!msg.contains("content")) {
                if (role == "assistant") {
                    continue;
                }
                oai_messages.push_back(msg);
                continue;
            }

            const json & content = msg.at("content");

            if (content.is_string()) {
                oai_messages.push_back(msg);
                continue;
            }

            if (!content.is_array()) {
                oai_messages.push_back(msg);
                continue;
            }

            json tool_calls = json::array();
            json converted_content = json::array();
            json tool_results = json::array();
            std::string reasoning_content;
            bool has_tool_calls = false;

            for (const auto & block : content) {
                std::string type = json_value(block, "type", std::string());

                if (type == "text") {
                    converted_content.push_back(block);
                } else if (type == "thinking") {
                    reasoning_content += json_value(block, "thinking", std::string());
                } else if (type == "image") {
                    json source = json_value(block, "source", json::object());
                    std::string source_type = json_value(source, "type", std::string());

                    if (source_type == "base64") {
                        std::string media_type = json_value(source, "media_type", std::string("image/jpeg"));
                        std::string data = json_value(source, "data", std::string());
                        std::ostringstream ss;
                        ss << "data:" << media_type << ";base64," << data;

                        converted_content.push_back({
                            {"type", "image_url"},
                            {"image_url", {
                                {"url", ss.str()}
                            }}
                        });
                    } else if (source_type == "url") {
                        std::string url = json_value(source, "url", std::string());
                        converted_content.push_back({
                            {"type", "image_url"},
                            {"image_url", {
                                {"url", url}
                            }}
                        });
                    }
                } else if (type == "tool_use") {
                    tool_calls.push_back({
                        {"id", json_value(block, "id", std::string())},
                        {"type", "function"},
                        {"function", {
                            {"name", json_value(block, "name", std::string())},
                            {"arguments", json_value(block, "input", json::object()).dump()}
                        }}
                    });
                    has_tool_calls = true;
                } else if (type == "tool_result") {
                    std::string tool_use_id = json_value(block, "tool_use_id", std::string());

                    auto result_content = json_value(block, "content", json());
                    if (result_content.is_string()) {
                        tool_results.push_back({
                            {"role", "tool"},
                            {"tool_call_id", tool_use_id},
                            {"content", result_content.get<std::string>()}
                        });
                    } else if (result_content.is_array()) {
                        // Single-pass: build both text and content_parts, decide format at the end
                        std::string result_text;
                        json content_parts = json::array();
                        bool has_images = false;

                        for (const auto & c : result_content) {
                            std::string c_type = json_value(c, "type", std::string());
                            if (c_type == "text") {
                                std::string text = json_value(c, "text", std::string());
                                result_text += text;
                                content_parts.push_back({
                                    {"type", "text"},
                                    {"text", text}
                                });
                            } else if (c_type == "image") {
                                has_images = true;
                                json source = json_value(c, "source", json::object());
                                std::string source_type = json_value(source, "type", std::string());
                                if (source_type == "base64") {
                                    std::string media_type = json_value(source, "media_type", std::string("image/jpeg"));
                                    std::string data = json_value(source, "data", std::string());
                                    std::string url = "data:" + media_type + ";base64," + data;
                                    content_parts.push_back({
                                        {"type", "image_url"},
                                        {"image_url", {{"url", url}}}
                                    });
                                } else if (source_type == "url") {
                                    content_parts.push_back({
                                        {"type", "image_url"},
                                        {"image_url", {{"url", json_value(source, "url", std::string())}}}
                                    });
                                }
                            }
                        }

                        if (!has_images) {
                            // Text-only: collapse to a plain string for maximum compatibility
                            tool_results.push_back({
                                {"role", "tool"},
                                {"tool_call_id", tool_use_id},
                                {"content", result_text}
                            });
                        } else {
                            // Mixed or image-only: use array content parts (OpenAI multimodal tool format)
                            tool_results.push_back({
                                {"role", "tool"},
                                {"tool_call_id", tool_use_id},
                                {"content", content_parts}
                            });
                        }
                    } else {
                        tool_results.push_back({
                            {"role", "tool"},
                            {"tool_call_id", tool_use_id},
                            {"content", ""}
                        });
                    }
                }
            }

            if (!converted_content.empty() || has_tool_calls || !reasoning_content.empty()) {
                json new_msg = {{"role", role}};
                if (!converted_content.empty()) {
                    new_msg["content"] = converted_content;
                } else if (has_tool_calls || !reasoning_content.empty()) {
                    new_msg["content"] = "";
                }
                if (!tool_calls.empty()) {
                    new_msg["tool_calls"] = tool_calls;
                }
                if (!reasoning_content.empty()) {
                    new_msg["reasoning_content"] = reasoning_content;
                }
                oai_messages.push_back(new_msg);
            }

            for (const auto & tool_msg : tool_results) {
                oai_messages.push_back(tool_msg);
            }
        }
    }

    oai_body["messages"] = oai_messages;

    // Convert tools
    if (body.contains("tools")) {
        const json & tools = body.at("tools");
        if (tools.is_array()) {
            json oai_tools = json::array();
            for (const auto & tool : tools) {
                oai_tools.push_back({
                    {"type", "function"},
                    {"function", {
                        {"name", json_value(tool, "name", std::string())},
                        {"description", json_value(tool, "description", std::string())},
                        {"parameters", tool.contains("input_schema") ? tool.at("input_schema") : json::object()}
                    }}
                });
            }
            oai_body["tools"] = oai_tools;
        }
    }

    // Convert tool_choice
    if (body.contains("tool_choice")) {
        const json & tc = body.at("tool_choice");
        if (tc.is_object()) {
            std::string type = json_value(tc, "type", std::string());
            if (type == "auto") {
                oai_body["tool_choice"] = "auto";
            } else if (type == "any" || type == "tool") {
                oai_body["tool_choice"] = "required";
            }
        }
    }

    // Convert stop_sequences to stop
    if (body.contains("stop_sequences")) {
        oai_body["stop"] = body.at("stop_sequences");
    }

    // Handle max_tokens (required in Anthropic, but we're permissive)
    if (body.contains("max_tokens")) {
        oai_body["max_tokens"] = body.at("max_tokens");
    } else {
        oai_body["max_tokens"] = 4096;
    }

    // Pass through common params
    for (const auto & key : {"temperature", "top_p", "top_k", "stream", "chat_template_kwargs"}) {
        if (body.contains(key)) {
            oai_body[key] = body.at(key);
        }
    }

    // Handle Anthropic-specific thinking param
    if (body.contains("thinking")) {
        json thinking = json_value(body, "thinking", json::object());
        std::string thinking_type = json_value(thinking, "type", std::string());
        if (thinking_type == "enabled") {
            int budget_tokens = json_value(thinking, "budget_tokens", 10000);
            oai_body["thinking_budget_tokens"] = budget_tokens;
        }
    }

    // Handle Anthropic-specific metadata param
    if (body.contains("metadata")) {
        json metadata = json_value(body, "metadata", json::object());
        std::string user_id = json_value(metadata, "user_id", std::string());
        if (!user_id.empty()) {
            oai_body["__metadata_user_id"] = user_id;
        }
    }

    return oai_body;
}

json server_chat_msg_diff_to_json_oaicompat(const common_chat_msg_diff & diff) {
    json delta = json::object();
    if (!diff.reasoning_content_delta.empty()) {
        delta["reasoning_content"] = diff.reasoning_content_delta;
    }
    if (!diff.content_delta.empty()) {
        delta["content"] = diff.content_delta;
    }
    if (diff.tool_call_index != std::string::npos) {
        json tool_call;
        tool_call["index"] = diff.tool_call_index;
        if (!diff.tool_call_delta.id.empty()) {
            tool_call["id"]   = diff.tool_call_delta.id;
            tool_call["type"] = "function";
        }
        if (!diff.tool_call_delta.name.empty() || !diff.tool_call_delta.arguments.empty()) {
            json function = json::object();
            if (!diff.tool_call_delta.name.empty()) {
                function["name"] = diff.tool_call_delta.name;
            }
            if (!diff.tool_call_delta.arguments.empty()) {
                function["arguments"] = diff.tool_call_delta.arguments;
            }
            tool_call["function"] = function;
        }
        delta["tool_calls"] = json::array({ tool_call });
    }
    return delta;
}

json convert_transcriptions_to_chatcmpl(
        const json & inp_body,
        const common_chat_templates * tmpls,
        const std::map<std::string, uploaded_file> & in_files,
        std::vector<raw_buffer> & out_files) {
    // TODO @ngxson : this function may need to be improved in the future
    // handle input files
    out_files.clear();
    auto it = in_files.find("file");
    if (it != in_files.end()) {
        out_files.push_back(it->second.data);
    } else {
        throw std::invalid_argument("No input file found for transcription");
    }

    // handle input data
    std::string prompt          = json_value(inp_body, "prompt", std::string());
    std::string language        = json_value(inp_body, "language", std::string());
    std::string response_format = json_value(inp_body, "response_format", std::string("json"));
    if (response_format != "json") {
        throw std::invalid_argument("Only 'json' response_format is supported for transcription");
    }
    const common_chat_prompt_preset preset = common_chat_get_asr_prompt(tmpls);
    if (prompt.empty()) {
        prompt = preset.user;
    }
    if (!language.empty()) {
        prompt += string_format(" (language: %s)", language.c_str());
    }
    prompt += get_media_marker();

    json messages = json::array();
    if (!preset.system.empty()) {
        messages.push_back({{"role", "system"}, {"content", preset.system}});
    }
    messages.push_back({{"role", "user"}, {"content", prompt}});

    json chatcmpl_body = inp_body; // copy all fields
    chatcmpl_body["messages"] = messages;

    // because input from form-data, everything is string, we need to correct the types here
    std::string stream = json_value(inp_body, "stream", std::string("false"));
    chatcmpl_body["stream"] = stream == "true";

    if (inp_body.contains("max_tokens")) {
        std::string inp = inp_body["max_tokens"].get<std::string>();
        chatcmpl_body["max_tokens"] = std::stoul(inp);
    }

    if (inp_body.contains("temperature")) {
        std::string inp = inp_body["temperature"].get<std::string>();
        chatcmpl_body["temperature"] = std::stof(inp);
    }

    return chatcmpl_body;
}
