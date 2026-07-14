#include "server-task.h"

#include <algorithm>
#include "build-info.h"
#include "server-chat.h"
#include "chat.h"
#include "common.h"
#include "json-schema-to-grammar.h"
#include "llama.h"
#include "sampling.h"
#include "speculative.h"
#include "server-common.h"

using json = nlohmann::ordered_json;

//
// task_params
//

json task_params::format_logit_bias(const std::vector<llama_logit_bias> & logit_bias) const {
    json data = json::array();
    for (const auto & lb : logit_bias) {
        data.push_back(json{
            {"bias", lb.bias},
            {"token", lb.token},
        });
    }
    return data;
}

json task_params::to_json(bool only_metrics) const {
    std::vector<std::string> samplers;
    samplers.reserve(sampling.samplers.size());
    for (const auto & sampler : sampling.samplers) {
        samplers.emplace_back(common_sampler_type_to_str(sampler));
    }

    json lora = json::array();
    for (auto & it : this->lora) {
        lora.push_back({{"id", it.first}, {"scale", it.second}});
    }

    if (only_metrics) {
        return json {
            {"seed",                      sampling.seed},
            {"temperature",               sampling.temp},
            {"dynatemp_range",            sampling.dynatemp_range},
            {"dynatemp_exponent",         sampling.dynatemp_exponent},
            {"top_k",                     sampling.top_k},
            {"top_p",                     sampling.top_p},
            {"min_p",                     sampling.min_p},
            {"top_n_sigma",               sampling.top_n_sigma},
            {"xtc_probability",           sampling.xtc_probability},
            {"xtc_threshold",             sampling.xtc_threshold},
            {"typical_p",                 sampling.typ_p},
            {"repeat_last_n",             sampling.penalty_last_n},
            {"repeat_penalty",            sampling.penalty_repeat},
            {"presence_penalty",          sampling.penalty_present},
            {"frequency_penalty",         sampling.penalty_freq},
            {"dry_multiplier",            sampling.dry_multiplier},
            {"dry_base",                  sampling.dry_base},
            {"dry_allowed_length",        sampling.dry_allowed_length},
            {"dry_penalty_last_n",        sampling.dry_penalty_last_n},
            {"mirostat",                  sampling.mirostat},
            {"mirostat_tau",              sampling.mirostat_tau},
            {"mirostat_eta",              sampling.mirostat_eta},
            {"max_tokens",                n_predict},
            {"n_predict",                 n_predict}, // TODO: deduplicate?
            {"n_keep",                    n_keep},
            {"n_discard",                 n_discard},
            {"ignore_eos",                sampling.ignore_eos},
            {"stream",                    stream},
            {"n_probs",                   sampling.n_probs},
            {"min_keep",                  sampling.min_keep},
            {"chat_format",               common_chat_format_name(chat_parser_params.format)},
            {"reasoning_format",          common_reasoning_format_name(chat_parser_params.reasoning_format)},
            {"reasoning_in_content",      chat_parser_params.reasoning_in_content},
            {"generation_prompt",         chat_parser_params.generation_prompt},
            {"samplers",                  samplers},
            {"speculative.types",         common_speculative_type_name_str(speculative.types)},
            {"timings_per_token",         timings_per_token},
            {"post_sampling_probs",       post_sampling_probs},
            {"backend_sampling",          sampling.backend_sampling},
            {"lora",                      lora},
        };
    }

    auto grammar_triggers = json::array();
    for (const auto & trigger : sampling.grammar_triggers) {
        server_grammar_trigger ct(trigger);
        grammar_triggers.push_back(ct.to_json());
    }

    return json {
        {"seed",                      sampling.seed},
        {"temperature",               sampling.temp},
        {"dynatemp_range",            sampling.dynatemp_range},
        {"dynatemp_exponent",         sampling.dynatemp_exponent},
        {"top_k",                     sampling.top_k},
        {"top_p",                     sampling.top_p},
        {"min_p",                     sampling.min_p},
        {"top_n_sigma",               sampling.top_n_sigma},
        {"xtc_probability",           sampling.xtc_probability},
        {"xtc_threshold",             sampling.xtc_threshold},
        {"typical_p",                 sampling.typ_p},
        {"repeat_last_n",             sampling.penalty_last_n},
        {"repeat_penalty",            sampling.penalty_repeat},
        {"presence_penalty",          sampling.penalty_present},
        {"frequency_penalty",         sampling.penalty_freq},
        {"dry_multiplier",            sampling.dry_multiplier},
        {"dry_base",                  sampling.dry_base},
        {"dry_allowed_length",        sampling.dry_allowed_length},
        {"dry_penalty_last_n",        sampling.dry_penalty_last_n},
        {"dry_sequence_breakers",     sampling.dry_sequence_breakers},
        {"mirostat",                  sampling.mirostat},
        {"mirostat_tau",              sampling.mirostat_tau},
        {"mirostat_eta",              sampling.mirostat_eta},
        {"stop",                      antiprompt},
        {"max_tokens",                n_predict},
        {"n_predict",                 n_predict}, // TODO: deduplicate?
        {"n_keep",                    n_keep},
        {"n_discard",                 n_discard},
        {"ignore_eos",                sampling.ignore_eos},
        {"stream",                    stream},
        {"logit_bias",                format_logit_bias(sampling.logit_bias)},
        {"n_probs",                   sampling.n_probs},
        {"min_keep",                  sampling.min_keep},
        {"grammar",                   common_grammar_value(sampling.grammar)},
        {"grammar_lazy",              sampling.grammar_lazy},
        {"grammar_triggers",          grammar_triggers},
        {"preserved_tokens",          sampling.preserved_tokens},
        {"chat_format",               common_chat_format_name(chat_parser_params.format)},
        {"reasoning_format",          common_reasoning_format_name(chat_parser_params.reasoning_format)},
        {"reasoning_in_content",      chat_parser_params.reasoning_in_content},
        {"generation_prompt",         chat_parser_params.generation_prompt},
        {"samplers",                  samplers},
        {"speculative.types",         common_speculative_type_name_str(speculative.types)},
        {"timings_per_token",         timings_per_token},
        {"post_sampling_probs",       post_sampling_probs},
        {"backend_sampling",          sampling.backend_sampling},
        {"lora",                      lora},
    };
}

//
// task_result_state
//
task_result_state::task_result_state(const common_chat_parser_params & chat_parser_params)
    : chat_parser_params(chat_parser_params)
    , oai_resp_id("resp_" + random_string())
    , oai_resp_reasoning_id("rs_" + random_string())
    , oai_resp_message_id("msg_" + random_string()) {
    if (chat_parser_params.is_continuation && !chat_parser_params.echo) {
        // initialize chat_msg to avoid emitting a delta containing the assistant prefill
        chat_msg = common_chat_parse("", true, chat_parser_params);
    }
}

common_chat_msg task_result_state::update_chat_msg(
        const std::string & text_added,
        bool is_partial,
        std::vector<common_chat_msg_diff> & diffs,
        bool filter_tool_calls) {
    generated_text += text_added;
    auto msg_prv_copy = chat_msg;
    //SRV_DBG("Parsing chat message: %s\n", generated_text.c_str());
    auto new_msg = common_chat_parse(
        generated_text,
        is_partial,
        chat_parser_params);

    // Try XML fallback when PEG parser found no tool calls
    // (Responses bridge: tools kept out of prompt, model still emits XML tool call tags)
    if (new_msg.tool_calls.empty()) {
        bool has_xml_tag = generated_text.find("<tool_call>") != std::string::npos ||
                           generated_text.find("<invoke") != std::string::npos ||
                           generated_text.find("<tool_search>") != std::string::npos;
        if (has_xml_tag) {
            parse_xml_tool_call_fallback(generated_text, is_partial,
                chat_parser_params.generation_prompt, new_msg, resp_tool_map);
        }
    }

    if (!new_msg.empty()) {
        new_msg.set_tool_call_ids(generated_tool_call_ids, gen_tool_call_id);
        chat_msg = new_msg;
        auto all_diffs = common_chat_msg_diff::compute_diffs(msg_prv_copy, chat_msg);

        if (!filter_tool_calls) {
            diffs = std::move(all_diffs);
        } else {
            for (auto & d : all_diffs) {
                // If this is a new type of delta, flush all currently pending tool call names
                for (size_t i = 0; i < chat_msg.tool_calls.size(); ++i) {
                    if (sent_tool_call_names.count(i) || chat_msg.tool_calls[i].name.empty()) {
                        continue;
                    }
                    if (d.tool_call_index != i || !d.tool_call_delta.arguments.empty()) {
                        common_chat_msg_diff header;
                        header.tool_call_index      = i;
                        header.tool_call_delta.id   = chat_msg.tool_calls[i].id;
                        header.tool_call_delta.name = chat_msg.tool_calls[i].name;
                        diffs.push_back(std::move(header));
                        sent_tool_call_names.insert(i);
                    }
                }

                if (d.tool_call_index == std::string::npos) {
                    diffs.push_back(std::move(d));
                } else {
                    size_t i = d.tool_call_index;
                    if (sent_tool_call_names.count(i)) {
                        if (!d.tool_call_delta.arguments.empty()) {
                            d.tool_call_delta.name = "";
                            d.tool_call_delta.id   = "";
                            diffs.push_back(std::move(d));
                        }
                    } else {
                        // Not sent yet.
                        if (!d.tool_call_delta.arguments.empty() || !is_partial) {
                            d.tool_call_delta.name = chat_msg.tool_calls[i].name;
                            d.tool_call_delta.id   = chat_msg.tool_calls[i].id;
                            diffs.push_back(std::move(d));
                            sent_tool_call_names.insert(i);
                        } else {
                            // Suppress
                        }
                    }
                }
            }
            // Final check at EOF
            if (!is_partial) {
                for (size_t i = 0; i < chat_msg.tool_calls.size(); ++i) {
                    if (!sent_tool_call_names.count(i) && !chat_msg.tool_calls[i].name.empty()) {
                        common_chat_msg_diff header;
                        header.tool_call_index      = i;
                        header.tool_call_delta.id   = chat_msg.tool_calls[i].id;
                        header.tool_call_delta.name = chat_msg.tool_calls[i].name;
                        diffs.push_back(std::move(header));
                        sent_tool_call_names.insert(i);
                    }
                }
            }
        }
    }
    return chat_msg;
}

//

// result_timings
//

json result_timings::to_json() const {
    json base = {
        {"cache_n",                cache_n},

        {"prompt_n",               prompt_n},
        {"prompt_ms",              prompt_ms},
        {"prompt_per_token_ms",    prompt_per_token_ms},
        {"prompt_per_second",      prompt_per_second},

        {"predicted_n",            predicted_n},
        {"predicted_ms",           predicted_ms},
        {"predicted_per_token_ms", predicted_per_token_ms},
        {"predicted_per_second",   predicted_per_second},
    };

    if (draft_n > 0) {
        base["draft_n"] = draft_n;
        base["draft_n_accepted"] = draft_n_accepted;
    }

    return base;
}

//
// result_prompt_progress
//
json result_prompt_progress::to_json() const {
    return json {
        {"total",     total},
        {"cache",     cache},
        {"processed", processed},
        {"time_ms",   time_ms},
    };
}

static inline std::string stop_type_to_str(stop_type type) {
    switch (type) {
        case STOP_TYPE_EOS:   return "eos";
        case STOP_TYPE_WORD:  return "word";
        case STOP_TYPE_LIMIT: return "limit";
        default:              return "none";
    }
}

//
// completion_token_output
//

json completion_token_output::to_json(bool post_sampling_probs) const {
    json probs_for_token = json::array();
    for (const auto & p : probs) {
        std::string txt(p.txt);
        txt.resize(validate_utf8(txt));
        probs_for_token.push_back(json {
            {"id",      p.tok},
            {"token",   txt},
            {"bytes",   str_to_bytes(p.txt)},
            {
                post_sampling_probs ? "prob" : "logprob",
                post_sampling_probs ? p.prob : logarithm(p.prob)
            },
        });
    }
    return probs_for_token;
}

json completion_token_output::probs_vector_to_json(const std::vector<completion_token_output> & probs, bool post_sampling_probs) {
    json out = json::array();
    for (const auto & p : probs) {
        std::string txt(p.text_to_send);
        txt.resize(validate_utf8(txt));
        out.push_back(json {
            {"id",           p.tok},
            {"token",        txt},
            {"bytes",        str_to_bytes(p.text_to_send)},
            {
                post_sampling_probs ? "prob" : "logprob",
                post_sampling_probs ? p.prob : logarithm(p.prob)
            },
            {
                post_sampling_probs ? "top_probs" : "top_logprobs",
                p.to_json(post_sampling_probs)
            },
        });
    }
    return out;
}

float completion_token_output::logarithm(float x) {
    // nlohmann::json converts -inf to null, so we need to prevent that
    return x == 0.0f ? std::numeric_limits<float>::lowest() : std::log(x);
}

std::vector<unsigned char> completion_token_output::str_to_bytes(const std::string & str) {
    std::vector<unsigned char> bytes;
    for (unsigned char c : str) {
        bytes.push_back(c);
    }
    return bytes;
}

//
// server_task_result_cmpl_final
//
json server_task_result_cmpl_final::to_json() {
    GGML_ASSERT(is_updated && "update() must be called before to_json()");
    switch (res_type) {
        case TASK_RESPONSE_TYPE_NONE:
            return to_json_non_oaicompat();
        case TASK_RESPONSE_TYPE_OAI_CMPL:
            return to_json_oaicompat();
        case TASK_RESPONSE_TYPE_OAI_CHAT:
            return stream ? to_json_oaicompat_chat_stream() : to_json_oaicompat_chat();
        case TASK_RESPONSE_TYPE_OAI_RESP:
            return stream ? to_json_oaicompat_resp_stream() : to_json_oaicompat_resp();
        case TASK_RESPONSE_TYPE_OAI_ASR:
            return to_json_oaicompat_asr();
        case TASK_RESPONSE_TYPE_ANTHROPIC:
            return stream ? to_json_anthropic_stream() : to_json_anthropic();
        default:
            GGML_ASSERT(false && "Invalid task_response_type");
    }
}

json server_task_result_cmpl_final::to_json_non_oaicompat() {
    json res = json {
        {"index",               index},
        {"content",             content},
        {"tokens",              tokens},
        {"id_slot",             id_slot},
        {"stop",                true},
        {"model",               oaicompat_model},
        {"tokens_predicted",    n_decoded},
        {"tokens_evaluated",    n_prompt_tokens},
        {"generation_settings", generation_params.to_json()},
        {"prompt",              prompt},
        {"has_new_line",        has_new_line},
        {"truncated",           truncated},
        {"stop_type",           stop_type_to_str(stop)},
        {"stopping_word",       stopping_word},
        {"tokens_cached",       n_tokens_cached},
        {"timings",             timings.to_json()},
    };
    if (!stream && !probs_output.empty()) {
        res["completion_probabilities"] = completion_token_output::probs_vector_to_json(probs_output, post_sampling_probs);
    }
    return response_fields.empty() ? res : json_get_nested_values(response_fields, res);
}

json server_task_result_cmpl_final::usage_json_oaicompat() {
    return json {
        {"completion_tokens", n_decoded},
        {"prompt_tokens",     n_prompt_tokens},
        {"total_tokens",      n_decoded + n_prompt_tokens},
        {"prompt_tokens_details", json { {"cached_tokens", n_prompt_tokens_cache} }},
    };
}

json server_task_result_cmpl_final::to_json_oaicompat() {
    std::time_t t = std::time(0);
    json logprobs = json(nullptr); // OAI default to null
    if (!stream && probs_output.size() > 0) {
        logprobs = json{
            {"content", completion_token_output::probs_vector_to_json(probs_output, post_sampling_probs)},
        };
    }
    json finish_reason = "length";
    if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
        finish_reason = "stop";
    }
    json res = json {
        {"choices",            json::array({
            json{
                {"text",          content},
                {"index",         index},
                {"logprobs",      logprobs},
                {"finish_reason", finish_reason},
            }
        })},
        {"created",            t},
        {"model",              oaicompat_model},
        {"system_fingerprint", std::string(llama_build_info())},
        {"object",             "text_completion"},
        {"usage",              usage_json_oaicompat()},
        {"id", oaicompat_cmpl_id}
    };

    // extra fields for debugging purposes
    if (verbose) {
        res["__verbose"] = to_json_non_oaicompat();
    }
    if (timings.prompt_n >= 0) {
        res.push_back({"timings", timings.to_json()});
    }

    return res;
}

json server_task_result_cmpl_final::to_json_oaicompat_chat() {
    std::string finish_reason = "length";
    common_chat_msg msg;
    if (!oaicompat_msg.empty()) {
        msg = oaicompat_msg;
    } else {
        msg.role = "assistant";
        msg.content = content;
    }
    if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
        if (msg.tool_calls.empty()) {
            finish_reason = "stop";
        } else if (all_tool_call_arguments_valid(msg.tool_calls)) {
            finish_reason = "tool_calls";
        } else {
            finish_reason = "length";
        }
    }

    json choice {
        {"finish_reason", finish_reason},
        {"index", index},
        {"message", msg.to_json_oaicompat()},
    };

    if (!stream && probs_output.size() > 0) {
        choice["logprobs"] = json{
            {"content", completion_token_output::probs_vector_to_json(probs_output, post_sampling_probs)},
        };
    }

    std::time_t t = std::time(0);

    json res = json {
        {"choices",            json::array({choice})},
        {"created",            t},
        {"model",              oaicompat_model},
        {"system_fingerprint", std::string(llama_build_info())},
        {"object",             "chat.completion"},
        {"usage",              usage_json_oaicompat()},
        {"id", oaicompat_cmpl_id}
    };

    // extra fields for debugging purposes
    if (verbose) {
        res["__verbose"] = to_json_non_oaicompat();
    }

    if (finish_reason == "length" && !msg.tool_calls.empty()) {
        res["__tool_call_incomplete"] = true;
    }

    if (timings.prompt_n >= 0) {
        res.push_back({"timings", timings.to_json()});
    }

    return res;
}

json server_task_result_cmpl_final::to_json_oaicompat_chat_stream() {
    std::time_t t = std::time(0);
    std::string finish_reason = "length";
    if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
        if (oaicompat_msg.tool_calls.empty()) {
            finish_reason = "stop";
        } else if (all_tool_call_arguments_valid(oaicompat_msg.tool_calls)) {
            finish_reason = "tool_calls";
        } else {
            finish_reason = "length";
        }
    }

    json deltas = json::array();
    for (const auto & diff : oaicompat_msg_diffs) {
        deltas.push_back({
            {"choices", json::array({
                json {
                    {"finish_reason", nullptr},
                    {"index", index},
                    {"delta", server_chat_msg_diff_to_json_oaicompat(diff)},
                },
            })},
            {"created", t},
            {"id", oaicompat_cmpl_id},
            {"model", oaicompat_model},
            {"system_fingerprint", std::string(llama_build_info())},
            {"object", "chat.completion.chunk"},
        });
    }

    deltas.push_back({
        {"choices", json::array({
            json {
                {"finish_reason", finish_reason},
                {"index", index},
                {"delta", json::object()},
            },
        })},
        {"created",            t},
        {"id",                 oaicompat_cmpl_id},
        {"model",              oaicompat_model},
        {"system_fingerprint", std::string(llama_build_info())},
        {"object",             "chat.completion.chunk"},
    });

    if (include_usage) {
        // OpenAI API spec for chat.completion.chunks specifies an empty `choices` array for the last chunk when including usage
        // https://platform.openai.com/docs/api-reference/chat_streaming/streaming#chat_streaming/streaming-choices
        deltas.push_back({
            {"choices", json::array()},
            {"created",            t},
            {"id",                 oaicompat_cmpl_id},
            {"model",              oaicompat_model},
            {"system_fingerprint", std::string(llama_build_info())},
            {"object",             "chat.completion.chunk"},
            {"usage",              usage_json_oaicompat()},
        });
    }

    if (timings.prompt_n >= 0) {
        deltas.back().push_back({"timings", timings.to_json()});
    }

    if (finish_reason == "length" && !oaicompat_msg.tool_calls.empty()) {
        deltas.back()["__tool_call_incomplete"] = true;
    }

    // extra fields for debugging purposes
    if (verbose && !deltas.empty()) {
        deltas.front()["__verbose"] = to_json_non_oaicompat();
    }

    return deltas;
}

// Helper functions for Responses API SSE formatting

static std::string build_output_text(const std::vector<json> & output) {
    std::string result;
    for (const auto & item : output) {
        if (json_value(item, "type", std::string()) == "message") {
            for (const auto & part : item.at("content")) {
                if (json_value(part, "type", std::string()) == "output_text") {
                    result += part.at("text").get<std::string>();
                }
            }
        }
    }
    return result;
}

static json build_oai_resp_metadata(const std::string & oai_resp_id,
                                    const std::string & oaicompat_model,
                                    const std::vector<json> & output,
                                    const std::string & output_text,
                                    int n_prompt_tokens,
                                    int n_decoded,
                                    int n_prompt_tokens_cache,
                                    const std::string & status = "completed") {
    std::time_t t = std::time(0);
    return json {
        {"completed_at",         status == "completed" ? json(t) : json(nullptr)},
        {"created_at",           t},
        {"id",                   oai_resp_id},
        {"model",                oaicompat_model},
        {"object",               "response"},
        {"output",               output},
        {"output_text",          output_text},
        {"status",               status},
        {"usage",                json {
            {"input_tokens",          n_prompt_tokens},
            {"output_tokens",         n_decoded},
            {"total_tokens",          n_decoded + n_prompt_tokens},
            {"input_tokens_details",  json{{"cached_tokens", n_prompt_tokens_cache}}},
            {"output_tokens_details", json{{"reasoning_tokens", 0}}},
        }},
        {"incomplete_details",   nullptr},
        {"previous_response_id", nullptr},
        {"instructions",         nullptr},
        {"error",                nullptr},
        {"tools",                json::array()},
        {"tool_choice",          "auto"},
        {"truncation",           "disabled"},
        {"parallel_tool_calls",  false},
        {"text",                 json{{"format", json{{"type", "text"}}}}},
        {"top_p",                1.0},
        {"presence_penalty",     0.0},
        {"frequency_penalty",    0.0},
        {"top_logprobs",         0},
        {"temperature",          1.0},
        {"reasoning",            nullptr},
        {"max_output_tokens",    nullptr},
        {"max_tool_calls",       nullptr},
        {"store",                false},
        {"background",           false},
        {"service_tier",         "default"},
        {"safety_identifier",    nullptr},
        {"prompt_cache_key",     nullptr},
        {"metadata",             json::object()},
    };
}

static json build_responses_function_call_item(
        const common_chat_tool_call & tool_call,
        const std::string & status,
        const std::string & item_id) {
    return json {
        {"type",      "function_call"},
        {"id",        item_id.empty() ? "fc_" + tool_call.id : item_id},
        {"call_id",   "call_" + tool_call.id},
        {"name",      tool_call.name},
        {"arguments", tool_call.arguments},
        {"status",    status},
    };
}

static json build_responses_reasoning_item(const std::string & id, const std::string & text, const std::string & status) {
    json item = {
        {"id",                id},
        {"summary",           json::array()},
        {"type",              "reasoning"},
        {"content",           json::array()},
        {"encrypted_content", ""},
        {"status",            status},
    };
    if (!text.empty()) {
        item["summary"].push_back({{"type", "summary_text"}, {"text", text}});
        item["content"].push_back({{"type", "reasoning_text"}, {"text", text}});
    }
    return item;
}

static json build_responses_content_part(const std::string & text) {
    return json {
        {"type", "output_text"}, {"annotations", json::array()},
        {"logprobs", json::array()}, {"text", text},
    };
}

static json build_responses_message_item(const std::string & id, const common_chat_msg & msg, const bool has_tool_calls) {
    return json {
        {"content", json::array({build_responses_content_part(msg.content)})},
        {"id",     id},
        {"status", "completed"},
        {"type",   "message"},
    };
}

static json build_responses_sse(const char * event, int & seq_num, const json & fields) {
    json data = {
        {"type",            event},
        {"sequence_number", seq_num++},
    };
    for (const auto & field : fields.items()) {
        data[field.key()] = field.value();
    }
    return json {{"event", event}, {"data", data}};
}


// Generic tool-call restorer that reads the preserved original_tool from the
// mapping entry and produces the correct output item for the Responses API.
// This enables protocol-preserving passthrough for all tool types including
// web_search, tool_search, custom, namespace, and function.
static json restore_tool_call_from_original(
    const common_chat_tool_call & tool_call,
    const std::string & item_id,
    const std::string & status,
    const nlohmann::ordered_json & mapping)
{
    const std::string orig_type = json_value(mapping, "original_type", std::string());

    if (orig_type == "web_search") {
        // Native web_search passthrough: restore as web_search_call with action
        json args_parsed = json::object();
        try {
            json parsed = json::parse(tool_call.arguments);
            if (parsed.is_object()) {
                args_parsed = parsed;
            }
        } catch (...) {}
        json action = {
            {"type", "search"},
        };
        for (auto & entry : args_parsed.items()) {
            action[entry.key()] = entry.value();
        }
        return json {
            {"id",      item_id},
            {"type",    "web_search_call"},
            {"status",  status},
            {"call_id", "call_" + tool_call.id},
            {"action",  action},
        };
    }

    if (orig_type == "function") {
        return json {
            {"id",        item_id},
            {"type",      "function_call"},
            {"status",    status},
            {"arguments", tool_call.arguments},
            {"call_id",   "call_" + tool_call.id},
            {"name",      json_value(mapping, "original_name", tool_call.name)},
        };
    }

    if (orig_type == "custom") {
        std::string raw_input = tool_call.arguments;
        try {
            json args = json::parse(tool_call.arguments);
            if (args.is_object() && args.contains("input") && args["input"].is_string()) {
                raw_input = args["input"].get<std::string>();
            }
        } catch (...) {}
        return json {
            {"id",      item_id},
            {"type",    "custom_tool_call"},
            {"status",  status},
            {"input",   raw_input},
            {"call_id", "call_" + tool_call.id},
            {"name",    json_value(mapping, "original_name", tool_call.name)},
        };
    }

    if (orig_type == "namespace") {
        return json {
            {"id",        item_id},
            {"type",      "function_call"},
            {"status",    status},
            {"arguments", tool_call.arguments},
            {"call_id",   "call_" + tool_call.id},
            {"name",      json_value(mapping, "original_name", tool_call.name)},
            {"namespace", json_value(mapping, "namespace_name", std::string())},
        };
    }

    if (orig_type == "tool_search") {
        json args_parsed = json::object();
        try {
            json parsed = json::parse(tool_call.arguments);
            if (parsed.is_object()) {
                args_parsed = parsed;
            }
        } catch (...) {}
        return json {
            {"id",        item_id},
            {"type",      "tool_search_call"},
            {"status",    status},
            {"call_id",   "call_" + tool_call.id},
            {"execution", json_value(mapping, "execution", std::string("sync"))},
            {"arguments", args_parsed},
        };
    }

    // Unknown type: fall back to function_call
    return json {
        {"id",        item_id},
        {"type",      "function_call"},
        {"status",    status},
        {"arguments", tool_call.arguments},
        {"call_id",   "call_" + tool_call.id},
        {"name",      sanitize_tool_name(tool_call.name)},
    };
}

// Restore original tool call type from mapping for Responses API round-trip.
// When the mapping entry contains "original_tool", delegates to the generic
// restore_tool_call_from_original helper for protocol-preserving passthrough.
// Falls back to legacy type-based logic when original_tool is absent.
static json restore_responses_tool_call(
    const common_chat_tool_call & tool_call,
    const std::string & fc_item_id,
    const std::string & status,
    const std::map<std::string, nlohmann::ordered_json> & tool_map)
{
    // Default: function_call with sanitized name
    const std::string default_name = sanitize_tool_name(tool_call.name);
    auto it = tool_map.find(default_name);
    if (it != tool_map.end()) {
        // New path: if original_tool is present, use generic helper
        if (it->second.contains("original_tool")) {
            return restore_tool_call_from_original(tool_call, fc_item_id, status, it->second);
        }
        // Legacy path: no original_tool, use existing type-based logic
        const std::string orig_type = json_value(it->second, "original_type", std::string());
        if (orig_type == "custom") {
            std::string raw_input = tool_call.arguments;
            try {
                json args = json::parse(tool_call.arguments);
                if (args.is_object() && args.contains("input") && args["input"].is_string()) {
                    raw_input = args["input"].get<std::string>();
                }
            } catch (...) {}
            return json {
                {"id",        fc_item_id},
                {"type",      "custom_tool_call"},
                {"status",    status},
                {"input",     raw_input},
                {"call_id",   "call_" + tool_call.id},
                {"name",      json_value(it->second, "original_name", tool_call.name)},
            };
        } else if (orig_type == "tool_search") {
            json args_parsed = json::object();
            try {
                json parsed = json::parse(tool_call.arguments);
                if (parsed.is_object()) {
                    args_parsed = parsed;
                }
            } catch (...) {}
            std::string execution = json_value(it->second, "execution", std::string("sync"));
            return json {
                {"id",            fc_item_id},
                {"type",          "tool_search_call"},
                {"status",        status},
                {"call_id",       "call_" + tool_call.id},
                {"execution",     execution},
                {"arguments",     args_parsed},
            };
        } else if (orig_type == "namespace") {
            return json {
                {"id",            fc_item_id},
                {"type",          "function_call"},
                {"status",        status},
                {"arguments",     tool_call.arguments},
                {"call_id",       "call_" + tool_call.id},
                {"name",          json_value(it->second, "original_name", tool_call.name)},
                {"namespace",     json_value(it->second, "namespace_name", std::string())},
            };
        } else if (orig_type == "web_search") {
            // Legacy web_search mapping (replacement bridge, no original_tool)
            const std::string repl_exposed = json_value(it->second, "replacement_exposed_name", std::string());
            const std::string repl_type = json_value(it->second, "replacement_original_type", std::string("function"));
            const std::string repl_name = json_value(it->second, "replacement_original_name", std::string());

            std::string args_out = tool_call.arguments;
            try {
                json args = json::parse(tool_call.arguments);
                if (args.is_object()) {
                    json repl_params = json_value(it->second, "replacement_parameters", json::object());
                    json repl_props = json_value(repl_params, "properties", json::object());
                    if (!repl_props.empty()) {
                        json remapped = json::object();
                        for (auto & prop : args.items()) {
                            const std::string & key = prop.key();
                            if (repl_props.contains(key)) {
                                remapped[key] = prop.value();
                            } else if (key == "query") {
                                if (repl_props.contains("q")) {
                                    remapped["q"] = prop.value();
                                } else if (repl_props.contains("search_query")) {
                                    remapped["search_query"] = prop.value();
                                }
                            }
                        }
                        if (!remapped.empty()) {
                            args_out = remapped.dump();
                        }
                    }
                }
            } catch (...) {}

            if (repl_type == "custom") {
                std::string raw_input = tool_call.arguments;
                try {
                    json args = json::parse(args_out);
                    if (args.is_object()) {
                        for (auto & prop : args.items()) {
                            if (prop.value().is_string()) {
                                raw_input = prop.value().get<std::string>();
                                break;
                            }
                        }
                    }
                } catch (...) {}
                return json {
                    {"id", fc_item_id}, {"type", "custom_tool_call"}, {"status", status},
                    {"input", raw_input}, {"call_id", "call_" + tool_call.id}, {"name", repl_name},
                };
            }
            return json {
                {"id", fc_item_id}, {"type", "function_call"}, {"status", status},
                {"arguments", args_out}, {"call_id", "call_" + tool_call.id},
                {"name", repl_name.empty() ? "web_search" : repl_name},
            };
        }
    }
    // Default: function_call with sanitized name
    return json {
        {"id",        fc_item_id},
        {"type",      "function_call"},
        {"status",    status},
        {"arguments", tool_call.arguments},
        {"call_id",   "call_" + tool_call.id},
        {"name",      default_name},
    };
}

json server_task_result_cmpl_final::to_json_oaicompat_resp() {
    common_chat_msg msg;
    if (!oaicompat_msg.empty()) {
        msg = oaicompat_msg;
    } else {
        msg.role = "assistant";
        msg.content = content;
    }

    std::vector<json> output;

    if (msg.reasoning_content != "") {
        output.push_back(build_responses_reasoning_item(
            "rs_" + random_string(), msg.reasoning_content, "completed"));
    }

    if (msg.content != "") {
        output.push_back(json {
            {"content", json::array({build_responses_content_part(msg.content)})},
            {"id",     "msg_" + random_string()},
            {"role",   msg.role},
            {"status", "completed"},
            {"type",   "message"},
        });
    }

    bool has_incomplete_tool = false;
    for (size_t i = 0; i < oaicompat_msg.tool_calls.size(); i++) {
        const auto & tool_call = oaicompat_msg.tool_calls[i];
        const std::string fc_item_id = (i < oai_resp_fc_item_ids.size())
            ? oai_resp_fc_item_ids[i]
            : "fc_" + random_string();
        const bool valid = tool_call_arguments_valid(tool_call);
        output.push_back(restore_responses_tool_call(
            tool_call, fc_item_id, valid ? "completed" : "incomplete",
            generation_params.responses_tool_map));
        if (!valid) {
            has_incomplete_tool = true;
        }
    }

    const std::string output_text = build_output_text(output);
    const std::string status = has_incomplete_tool ? "incomplete" : "completed";
    json resp = build_oai_resp_metadata(oai_resp_id, oaicompat_model, output, output_text,
        n_prompt_tokens, n_decoded, n_prompt_tokens_cache, status);
    if (has_incomplete_tool) {
        resp["incomplete_details"] = json{{"reason", "incomplete_tool_call"}};
    }

    // [RESP_CTX] response_final (non-streaming)
    if (resp_ctx_debug_enabled() && !generation_params.responses_tool_map.empty()) {
        size_t n_msg = 0, n_fn = 0, n_cust = 0, n_ns = 0, n_ts = 0, n_ws = 0, n_incomplete = 0;
        for (const auto & item : output) {
            std::string t = json_value(item, "type", std::string());
            if (t == "message")              n_msg++;
            else if (t == "function_call")    n_fn++;
            else if (t == "custom_tool_call") n_cust++;
            else if (t == "tool_search_call") n_ts++;
            else if (t == "web_search_call")  n_ws++;
            std::string s = json_value(item, "status", std::string());
            if (s == "incomplete") n_incomplete++;
        }
        int nt = n_prompt_tokens + n_decoded;
        SRV_CNT("[RESP_CTX] stage=response_final"
                " task=%d input_tokens=%d cached_tokens=%d output_tokens=%d total_tokens=%d"
                " items=%zu msg=%zu fn=%zu cust=%zu ts=%zu ws=%zu incomplete=%zu status=%s streaming=0\n",
                id, n_prompt_tokens, n_prompt_tokens_cache, n_decoded, nt,
                output.size(), n_msg, n_fn, n_cust, n_ts, n_ws, n_incomplete, status.c_str());
        // [RESP_CTX][WARN] if total_tokens != input+output (impossible, but check anyway)
        if (nt != n_prompt_tokens + n_decoded) {
            SRV_CNT("[RESP_CTX][WARN] stage=response_final: total_tokens mismatch input_tokens=%d output_tokens=%d total_tokens=%d\n",
                    n_prompt_tokens, n_decoded, nt);
        }
    }

    return resp;
}

json server_task_result_cmpl_final::to_json_oaicompat_resp_stream() {
    std::vector<json> server_sent_events;
    std::vector<json> output;
    int seq_num = oai_resp_seq_num;
    int output_idx = 0;

    if (oaicompat_msg.reasoning_content != "") {
        const json output_item = {
            {"id",                oai_resp_reasoning_id},
            {"summary",           json::array({json{{"type", "summary_text"}, {"text", oaicompat_msg.reasoning_content}}})},
            {"type",              "reasoning"},
            {"content",           json::array({json{{"text", oaicompat_msg.reasoning_content}, {"type", "reasoning_text"}}})},
            {"encrypted_content", ""},
            {"status",            "completed"},
        };
        server_sent_events.push_back(json {{"event", "response.output_item.done"}, {"data", json{
            {"type", "response.output_item.done"}, {"sequence_number", seq_num++},
            {"output_index", output_idx}, {"item", output_item},
        }}});
        output.push_back(output_item);
        output_idx++;
    }

    if (oaicompat_msg.content != "") {
        server_sent_events.push_back(json {{"event", "response.output_text.done"}, {"data", json{
            {"type", "response.output_text.done"}, {"sequence_number", seq_num++},
            {"output_index", output_idx}, {"content_index", 0},
            {"item_id", oai_resp_message_id}, {"text", oaicompat_msg.content},
            {"logprobs", json::array()},
        }}});

        const json content_part = {
            {"type", "output_text"}, {"annotations", json::array()},
            {"logprobs", json::array()}, {"text", oaicompat_msg.content},
        };
        server_sent_events.push_back(json {{"event", "response.content_part.done"}, {"data", json{
            {"type", "response.content_part.done"}, {"sequence_number", seq_num++},
            {"output_index", output_idx}, {"content_index", 0},
            {"item_id", oai_resp_message_id}, {"part", content_part},
        }}});

        const json output_item = {
            {"type", "message"}, {"status", "completed"},
            {"id", oai_resp_message_id},
            {"content", json::array({content_part})}, {"role", "assistant"},
        };
        server_sent_events.push_back(json {{"event", "response.output_item.done"}, {"data", json{
            {"type", "response.output_item.done"}, {"sequence_number", seq_num++},
            {"output_index", output_idx}, {"item", output_item},
        }}});
        output.push_back(output_item);
        output_idx++;
    }

    bool has_incomplete_tool = false;
    for (size_t i = 0; i < oaicompat_msg.tool_calls.size(); i++) {
        const auto & tool_call = oaicompat_msg.tool_calls[i];
        const std::string fc_item_id = (i < oai_resp_fc_item_ids.size())
            ? oai_resp_fc_item_ids[i]
            : "fc_" + random_string();
        const bool valid = tool_call_arguments_valid(tool_call);
        const json restored = restore_responses_tool_call(
            tool_call, fc_item_id, valid ? "completed" : "incomplete",
            generation_params.responses_tool_map);
        const std::string restored_type = json_value(restored, "type", std::string("function_call"));
        const std::string restored_name = json_value(restored, "name", tool_call.name);

        // function_call_arguments.done only for native function_call items
        if (valid && restored_type == "function_call") {
            // function_call_arguments.done - only for complete function_call items
            server_sent_events.push_back(json {{"event", "response.function_call_arguments.done"}, {"data", json{
                {"type", "response.function_call_arguments.done"}, {"sequence_number", seq_num++},
                {"output_index", output_idx},
                {"item_id", fc_item_id}, {"name", restored_name},
            }}});
        }

        const json output_item = restored;
        server_sent_events.push_back(json {{"event", "response.output_item.done"}, {"data", json{
            {"type", "response.output_item.done"}, {"sequence_number", seq_num++},
            {"output_index", output_idx}, {"item", output_item},
        }}});
        output.push_back(output_item);
        output_idx++;
        if (!valid) {
            has_incomplete_tool = true;
        }
    }

    // response.completed
    const std::string output_text = build_output_text(output);
    const std::string final_status = has_incomplete_tool ? "incomplete" : "completed";
    json completed_resp = build_oai_resp_metadata(oai_resp_id, oaicompat_model, output, output_text,
        n_prompt_tokens, n_decoded, n_prompt_tokens_cache, final_status);
    if (has_incomplete_tool) {
        completed_resp["incomplete_details"] = json{{"reason", "incomplete_tool_call"}};
    }
    // [RESP_CTX] response_final (streaming)
    if (resp_ctx_debug_enabled() && !generation_params.responses_tool_map.empty()) {
        size_t n_msg = 0, n_fn = 0, n_cust = 0, n_ns = 0, n_ts = 0, n_ws = 0, n_incomplete = 0;
        for (const auto & item : output) {
            std::string t = json_value(item, "type", std::string());
            if (t == "message")              n_msg++;
            else if (t == "function_call")    n_fn++;
            else if (t == "custom_tool_call") n_cust++;
            else if (t == "tool_search_call") n_ts++;
            else if (t == "web_search_call")  n_ws++;
            std::string s = json_value(item, "status", std::string());
            if (s == "incomplete") n_incomplete++;
        }
        int nt = n_prompt_tokens + n_decoded;
        SRV_CNT("[RESP_CTX] stage=response_final"
                " task=%d input_tokens=%d cached_tokens=%d output_tokens=%d total_tokens=%d"
                " items=%zu msg=%zu fn=%zu cust=%zu ts=%zu ws=%zu incomplete=%zu status=%s streaming=1\n",
                id, n_prompt_tokens, n_prompt_tokens_cache, n_decoded, nt,
                output.size(), n_msg, n_fn, n_cust, n_ts, n_ws, n_incomplete, final_status.c_str());
        if (nt != n_prompt_tokens + n_decoded) {
            SRV_CNT("[RESP_CTX][WARN] stage=response_final: total_tokens mismatch input_tokens=%d output_tokens=%d total_tokens=%d\n",
                    n_prompt_tokens, n_decoded, nt);
        }
    }

    server_sent_events.push_back(json {{"event", "response.completed"}, {"data", json{
        {"type", "response.completed"}, {"sequence_number", seq_num++},
        {"response", completed_resp},
    }}});

    return server_sent_events;
}

json server_task_result_cmpl_final::to_json_oaicompat_asr() {
    json event = json {
        {"type",  "transcript.text.done"},
        {"text",  oaicompat_msg.content},
        {"usage", json {
            {"type",         "tokens"},
            {"input_tokens",  n_prompt_tokens},
            {"output_tokens", n_decoded},
            {"total_tokens",  n_decoded + n_prompt_tokens},
            {"input_tokens_details", json { {"cached_tokens", n_prompt_tokens_cache} }},
        }},
    };
    return event;
}

json server_task_result_cmpl_final::to_json_anthropic() {
    std::string stop_reason = "max_tokens";
    if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
        stop_reason = oaicompat_msg.tool_calls.empty() ? "end_turn" : "tool_use";
    }

    json content_blocks = json::array();

    common_chat_msg msg;
    if (!oaicompat_msg.empty()) {
        msg = oaicompat_msg;
    } else {
        msg.role = "assistant";
        msg.content = content;
    }

    // thinking block comes first (Anthropic extended thinking format)
    if (!msg.reasoning_content.empty()) {
        content_blocks.push_back({
            {"type", "thinking"},
            {"thinking", msg.reasoning_content},
            {"signature", ""}  // empty signature for local models (no cryptographic verification)
        });
    }

    if (!msg.content.empty()) {
        content_blocks.push_back({
            {"type", "text"},
            {"text", msg.content}
        });
    }

    for (const auto & tool_call : msg.tool_calls) {
        json tool_use_block = {
            {"type", "tool_use"},
            {"id", tool_call.id},
            {"name", tool_call.name}
        };

        try {
            tool_use_block["input"] = json::parse(tool_call.arguments);
        } catch (const std::exception &) {
            tool_use_block["input"] = json::object();
        }

        content_blocks.push_back(tool_use_block);
    }

    json res = {
        {"id", oaicompat_cmpl_id},
        {"type", "message"},
        {"role", "assistant"},
        {"content", content_blocks},
        {"model", oaicompat_model},
        {"stop_reason", stop_reason},
        {"stop_sequence", stopping_word.empty() ? nullptr : json(stopping_word)},
        {"usage", {
            {"cache_read_input_tokens", n_prompt_tokens_cache},
            {"input_tokens", n_prompt_tokens - n_prompt_tokens_cache},
            {"output_tokens", n_decoded}
        }}
    };

    return res;
}

json server_task_result_cmpl_final::to_json_anthropic_stream() {
    json events = json::array();

    std::string stop_reason = "max_tokens";
    if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
        stop_reason = oaicompat_msg.tool_calls.empty() ? "end_turn" : "tool_use";
    }

    bool has_thinking = !oaicompat_msg.reasoning_content.empty();
    bool has_text     = !oaicompat_msg.content.empty();
    size_t num_tool_calls = oaicompat_msg.tool_calls.size();

    // content block indices: thinking (0) -> text (0 or 1) -> tool_use (n+)
    size_t thinking_block_index = 0;
    size_t text_block_index     = has_thinking ? 1 : 0;

    bool thinking_block_started = false;
    bool text_block_started     = false;
    std::unordered_set<size_t> tool_calls_started;

    for (const auto & diff : oaicompat_msg_diffs) {
        // handle thinking/reasoning content
        if (!diff.reasoning_content_delta.empty()) {
            if (!thinking_block_started) {
                events.push_back({
                    {"event", "content_block_start"},
                    {"data", {
                        {"type", "content_block_start"},
                        {"index", thinking_block_index},
                        {"content_block", {
                            {"type", "thinking"},
                            {"thinking", ""}
                        }}
                    }}
                });
                thinking_block_started = true;
            }

            events.push_back({
                {"event", "content_block_delta"},
                {"data", {
                    {"type", "content_block_delta"},
                    {"index", thinking_block_index},
                    {"delta", {
                        {"type", "thinking_delta"},
                        {"thinking", diff.reasoning_content_delta}
                    }}
                }}
            });
        }

        // handle regular text content
        if (!diff.content_delta.empty()) {
            if (!text_block_started) {
                events.push_back({
                    {"event", "content_block_start"},
                    {"data", {
                        {"type", "content_block_start"},
                        {"index", text_block_index},
                        {"content_block", {
                            {"type", "text"},
                            {"text", ""}
                        }}
                    }}
                });
                text_block_started = true;
            }

            events.push_back({
                {"event", "content_block_delta"},
                {"data", {
                    {"type", "content_block_delta"},
                    {"index", text_block_index},
                    {"delta", {
                        {"type", "text_delta"},
                        {"text", diff.content_delta}
                    }}
                }}
            });
        }

        // handle tool calls
        if (diff.tool_call_index != std::string::npos) {
            size_t content_block_index = (has_thinking ? 1 : 0) + (has_text ? 1 : 0) + diff.tool_call_index;

            if (tool_calls_started.find(diff.tool_call_index) == tool_calls_started.end()) {
                const auto & full_tool_call = oaicompat_msg.tool_calls[diff.tool_call_index];

                events.push_back({
                    {"event", "content_block_start"},
                    {"data", {
                        {"type", "content_block_start"},
                        {"index", content_block_index},
                        {"content_block", {
                            {"type", "tool_use"},
                            {"id", full_tool_call.id},
                            {"name", full_tool_call.name}
                        }}
                    }}
                });
                tool_calls_started.insert(diff.tool_call_index);
            }

            if (!diff.tool_call_delta.arguments.empty()) {
                events.push_back({
                    {"event", "content_block_delta"},
                    {"data", {
                        {"type", "content_block_delta"},
                        {"index", content_block_index},
                        {"delta", {
                            {"type", "input_json_delta"},
                            {"partial_json", diff.tool_call_delta.arguments}
                        }}
                    }}
                });
            }
        }
    }

    // close content blocks in order
    if (has_thinking) {
        // Anthropic API requires a signature_delta before closing thinking blocks
        // We use an empty signature since we can't generate a cryptographic signature for local models
        events.push_back({
            {"event", "content_block_delta"},
            {"data", {
                {"type", "content_block_delta"},
                {"index", thinking_block_index},
                {"delta", {
                    {"type", "signature_delta"},
                    {"signature", ""}
                }}
            }}
        });
        events.push_back({
            {"event", "content_block_stop"},
            {"data", {
                {"type", "content_block_stop"},
                {"index", thinking_block_index}
            }}
        });
    }

    if (has_text) {
        events.push_back({
            {"event", "content_block_stop"},
            {"data", {
                {"type", "content_block_stop"},
                {"index", text_block_index}
            }}
        });
    }

    for (size_t i = 0; i < num_tool_calls; i++) {
        size_t content_block_index = (has_thinking ? 1 : 0) + (has_text ? 1 : 0) + i;
        events.push_back({
            {"event", "content_block_stop"},
            {"data", {
                {"type", "content_block_stop"},
                {"index", content_block_index}
            }}
        });
    }

    events.push_back({
        {"event", "message_delta"},
        {"data", {
            {"type", "message_delta"},
            {"delta", {
                {"stop_reason", stop_reason},
                {"stop_sequence", stopping_word.empty() ? nullptr : json(stopping_word)}
            }},
            {"usage", {
                {"output_tokens", n_decoded}
            }}
        }}
    });

    events.push_back({
        {"event", "message_stop"},
        {"data", {
            {"type", "message_stop"}
        }}
    });

    return events;
}

//
// server_task_result_cmpl_partial
//
void server_task_result_cmpl_partial::update(task_result_state & state) {
    is_updated = true;
    if (is_begin) {
        return; // begin marker only flushes headers, skip parsing
    }
    state.update_chat_msg(content, true, oaicompat_msg_diffs);

    // Copy current state for use in to_json_*() (reflects state BEFORE this chunk)
    thinking_block_started = state.thinking_block_started;
    text_block_started     = state.text_block_started;

    oai_resp_created       = state.oai_resp_created;
    oai_resp_id            = state.oai_resp_id;
    oai_resp_reasoning_id  = state.oai_resp_reasoning_id;
    oai_resp_message_id    = state.oai_resp_message_id;
    oai_resp_fc_id         = state.oai_resp_fc_id;
    oai_resp_fc_item_id    = state.oai_resp_fc_item_id;
    oai_resp_seq_num       = state.oai_resp_seq_num;
    oai_resp_output_idx    = state.oai_resp_output_idx;
    oai_resp_reasoning_output_idx = state.oai_resp_reasoning_output_idx;
    oai_resp_reasoning_done       = state.oai_resp_reasoning_done;
    oai_resp_message_done         = state.oai_resp_message_done;
    oai_resp_reasoning_content    = state.chat_msg.reasoning_content;
    oai_resp_message_content      = state.chat_msg.content;

    // track if the accumulated message has any reasoning content
    anthropic_has_reasoning = !state.chat_msg.reasoning_content.empty();

    if (res_type == TASK_RESPONSE_TYPE_OAI_RESP && !state.oai_resp_created && (is_progress || n_decoded == 1)) {
        state.oai_resp_created = true;
        state.oai_resp_seq_num += 2; // response.created + response.in_progress
    }

    // Pre-compute state updates based on diffs (for next chunk)
    for (const common_chat_msg_diff & diff : oaicompat_msg_diffs) {
        if (!diff.reasoning_content_delta.empty()) {
            if (!state.thinking_block_started) {
                state.thinking_block_started = true;
                state.oai_resp_seq_num++; // output_item.added
                state.oai_resp_output_idx++;
            }
            state.oai_resp_seq_num++; // reasoning_text.delta
        }
        if (!diff.content_delta.empty()) {
            if (!state.text_block_started) {
                state.text_block_started = true;
                state.oai_resp_seq_num += 2; // output_item.added + content_part.added
                state.oai_resp_output_idx++;
            }
            state.oai_resp_seq_num++; // output_text.delta
        }
        if (!diff.tool_call_delta.name.empty()) {
            state.oai_resp_fc_id = diff.tool_call_delta.id;
            // Generate stable fc_ item ID for this tool call
            state.oai_resp_fc_item_id = "fc_" + random_string();
            state.oai_resp_fc_item_ids.push_back(state.oai_resp_fc_item_id);
            state.oai_resp_seq_num++; // output_item.added
            state.oai_resp_output_idx++;
        }
        if (!diff.tool_call_delta.arguments.empty()) {
            state.oai_resp_seq_num++; // function_call_arguments.delta
        }
    }
}

json server_task_result_cmpl_partial::to_json() {
    GGML_ASSERT(is_updated && "update() must be called before to_json()");
    if (is_begin) {
        return nullptr; // simply signal to HTTP handler to send the headers and status code
    }
    switch (res_type) {
        case TASK_RESPONSE_TYPE_NONE:
            return to_json_non_oaicompat();
        case TASK_RESPONSE_TYPE_OAI_CMPL:
            return to_json_oaicompat();
        case TASK_RESPONSE_TYPE_OAI_CHAT:
            return to_json_oaicompat_chat();
        case TASK_RESPONSE_TYPE_OAI_RESP:
            return to_json_oaicompat_resp();
        case TASK_RESPONSE_TYPE_OAI_ASR:
            return to_json_oaicompat_asr();
        case TASK_RESPONSE_TYPE_ANTHROPIC:
            return to_json_anthropic();
        default:
            GGML_ASSERT(false && "Invalid task_response_type");
    }
}

json server_task_result_cmpl_partial::to_json_non_oaicompat() {
    // non-OAI-compat JSON
    json res = json {
        {"index",            index},
        {"content",          content},
        {"tokens",           tokens},
        {"stop",             false},
        {"id_slot",          id_slot},
        {"tokens_predicted", n_decoded},
        {"tokens_evaluated", n_prompt_tokens},
    };
    // populate the timings object when needed (usually for the last response or with timings_per_token enabled)
    if (timings.prompt_n > 0) {
        res.push_back({"timings", timings.to_json()});
    }
    if (is_progress) {
        res.push_back({"prompt_progress", progress.to_json()});
    }
    if (!prob_output.probs.empty()) {
        res["completion_probabilities"] = completion_token_output::probs_vector_to_json({prob_output}, post_sampling_probs);
    }
    return res;
}

json server_task_result_cmpl_partial::to_json_oaicompat() {
    std::time_t t = std::time(0);
    json logprobs = json(nullptr); // OAI default to null
    if (prob_output.probs.size() > 0) {
        logprobs = json{
            {"content", completion_token_output::probs_vector_to_json({prob_output}, post_sampling_probs)},
        };
    }
    json res = json {
        {"choices",            json::array({
            json{
                {"text",          content},
                {"index",         index},
                {"logprobs",      logprobs},
                {"finish_reason", nullptr},
            }
        })},
        {"created",            t},
        {"model",              oaicompat_model},
        {"system_fingerprint", std::string(llama_build_info())},
        {"object",             "text_completion"},
        {"id",                 oaicompat_cmpl_id}
    };

    // extra fields for debugging purposes
    if (verbose) {
        res["__verbose"] = to_json_non_oaicompat();
    }
    if (timings.prompt_n >= 0) {
        res.push_back({"timings", timings.to_json()});
    }
    if (is_progress) {
        res.push_back({"prompt_progress", progress.to_json()});
    }

    return res;
}

json server_task_result_cmpl_partial::to_json_oaicompat_chat() {
    bool first = n_decoded == 1;
    std::time_t t = std::time(0);
    json choices;

    std::vector<json> deltas;
    auto add_delta = [&](const json & delta) {
        deltas.push_back({
            {"choices", json::array({
                json {
                    {"finish_reason", nullptr},
                    {"index", index},
                    {"delta", delta},
                },
            })},
            {"created", t},
            {"id", oaicompat_cmpl_id},
            {"model", oaicompat_model},
            {"system_fingerprint", std::string(llama_build_info())},
            {"object", "chat.completion.chunk"},
        });
    };
    // We have to send an initial update to conform to openai behavior
    if (first || is_progress) {
        add_delta({
            {"role", "assistant"},
            {"content", nullptr},
        });
    }

    for (const auto & diff : oaicompat_msg_diffs) {
        add_delta(server_chat_msg_diff_to_json_oaicompat(diff));
    }

    if (!deltas.empty()) {
        auto & last_json = deltas[deltas.size() - 1];
        GGML_ASSERT(last_json.at("choices").size() >= 1);

        if (prob_output.probs.size() > 0) {
            last_json.at("choices").at(0)["logprobs"] = json {
                {"content", completion_token_output::probs_vector_to_json({prob_output}, post_sampling_probs)},
            };
        }

        if (timings.prompt_n >= 0) {
            last_json.push_back({"timings", timings.to_json()});
        }
        if (is_progress) {
            last_json.push_back({"prompt_progress", progress.to_json()});
        }
    }

    return deltas;
}

json server_task_result_cmpl_partial::to_json_oaicompat_resp() {
    std::vector<json> events;

    int seq_num = oai_resp_seq_num;
    int output_idx = oai_resp_output_idx;
    auto add_seq = [&](json & data) {
        data["sequence_number"] = seq_num++;
    };

    if (!oai_resp_created) {
        json data = {{"type", "response.created"}};
        add_seq(data);
        data["response"] = json {{
            {"id",     oai_resp_id},
            {"object", "response"},
            {"status", "in_progress"},
        }};
        events.push_back({{"event", "response.created"}, {"data", data}});

        data = {{"type", "response.in_progress"}};
        add_seq(data);
        data["response"] = json {{
            {"id",     oai_resp_id},
            {"object", "response"},
            {"status", "in_progress"},
        }};
        events.push_back({{"event", "response.in_progress"}, {"data", data}});
    } else if (is_progress) {
        json data = {{"type", "response.in_progress"}};
        add_seq(data);
        data["response"] = json {{
            {"id",     oai_resp_id},
            {"object", "response"},
            {"status", "in_progress"},
        }};
        events.push_back({{"event", "response.in_progress"}, {"data", data}});
    }

    for (const common_chat_msg_diff & diff : oaicompat_msg_diffs) {
        if (!diff.reasoning_content_delta.empty()) {
            if (!thinking_block_started) {
                json data = {
                    {"type",  "response.output_item.added"},
                    {"output_index", output_idx},
                    {"item", json {
                        {"id",                oai_resp_reasoning_id},
                        {"summary",           json::array()},
                        {"type",              "reasoning"},
                        {"content",           json::array()},
                        {"encrypted_content", ""},
                        {"status",            "in_progress"},
                    }},
                };
                add_seq(data);
                events.push_back({{"event", "response.output_item.added"}, {"data", data}});
                thinking_block_started = true;
            }
            {
                json data = {
                    {"type",    "response.reasoning_text.delta"},
                    {"delta",   diff.reasoning_content_delta},
                    {"item_id", oai_resp_reasoning_id},
                };
                add_seq(data);
                events.push_back({{"event", "response.reasoning_text.delta"}, {"data", data}});
            }
        }

        if (!diff.content_delta.empty()) {
            if (!text_block_started) {
                {
                    json data = {
                        {"type",  "response.output_item.added"},
                        {"output_index", output_idx},
                        {"item", json {
                            {"content", json::array()},
                            {"id",      oai_resp_message_id},
                            {"role",    "assistant"},
                            {"status",  "in_progress"},
                            {"type",    "message"},
                        }},
                    };
                    add_seq(data);
                    events.push_back({{"event", "response.output_item.added"}, {"data", data}});
                }
                {
                    json data = {
                        {"type",    "response.content_part.added"},
                        {"output_index", output_idx},
                        {"content_index", 0},
                        {"item_id", oai_resp_message_id},
                        {"part", json {
                            {"type", "output_text"},
                            {"text", ""},
                        }},
                    };
                    add_seq(data);
                    events.push_back({{"event", "response.content_part.added"}, {"data", data}});
                }
                text_block_started = true;
            }
            {
                json data = {
                    {"type",    "response.output_text.delta"},
                    {"output_index", output_idx},
                    {"content_index", 0},
                    {"item_id", oai_resp_message_id},
                    {"delta",   diff.content_delta},
                };
                add_seq(data);
                events.push_back({{"event", "response.output_text.delta"}, {"data", data}});
            }
        }

        if (!diff.tool_call_delta.name.empty()) {
            const std::string fc_item_id = oai_resp_fc_item_id.empty()
                ? "fc_" + random_string()
                : oai_resp_fc_item_id;
            json data = {
                {"type",  "response.output_item.added"},
                {"output_index", output_idx},
                {"item", json {
                    {"id",        fc_item_id},
                    {"arguments", ""},
                    {"call_id",   "call_" + diff.tool_call_delta.id},
                    {"name",      diff.tool_call_delta.name},
                    {"type",      "function_call"},
                    {"status",    "in_progress"},
                }},
            };
            add_seq(data);
            events.push_back({{"event", "response.output_item.added"}, {"data", data}});
            oai_resp_fc_id = diff.tool_call_delta.id;
        }

        if (!diff.tool_call_delta.arguments.empty()) {
            const std::string fc_item_id = oai_resp_fc_item_id.empty()
                ? "fc_" + oai_resp_fc_id
                : oai_resp_fc_item_id;
            json data = {
                {"type",    "response.function_call_arguments.delta"},
                {"delta",   diff.tool_call_delta.arguments},
                {"item_id", fc_item_id},
                {"output_index", output_idx},
            };
            add_seq(data);
            events.push_back({{"event", "response.function_call_arguments.delta"}, {"data", data}});
        }
    }

    if (!events.empty()) {
        json & data = events.back().at("data");
        if (timings.prompt_n >= 0) {
            data.push_back({"timings", timings.to_json()});
        }
        if (is_progress) {
            data.push_back({"prompt_progress", progress.to_json()});
        }
    }

    return events;
}

json server_task_result_cmpl_partial::to_json_oaicompat_asr() {
    json event = json {
        {"type", "transcript.text.delta"},
        {"delta", content},
    };
    return event;
}

json server_task_result_cmpl_partial::to_json_anthropic() {
    json events = json::array();
    bool first = (n_decoded == 1);
    // use member variables to track block state across streaming calls
    // (anthropic_thinking_block_started, anthropic_text_block_started)

    if (first) {
        events.push_back({
            {"event", "message_start"},
            {"data", {
                {"type", "message_start"},
                {"message", {
                    {"id", oaicompat_cmpl_id},
                    {"type", "message"},
                    {"role", "assistant"},
                    {"content", json::array()},
                    {"model", oaicompat_model},
                    {"stop_reason", nullptr},
                    {"stop_sequence", nullptr},
                    {"usage", {
                        {"cache_read_input_tokens", n_prompt_tokens_cache},
                        {"input_tokens", n_prompt_tokens - n_prompt_tokens_cache},
                        {"output_tokens", 0}
                    }}
                }}
            }}
        });
    }

    // content block indices: thinking (0) -> text (0 or 1) -> tool_use (n+)
    size_t thinking_block_index = 0;
    // use anthropic_has_reasoning (set in update()) to know if ANY reasoning was generated
    size_t text_block_index     = anthropic_has_reasoning ? 1 : 0;

    // use local copies of streaming state (copied from task_result_state in update())
    // these reflect the state BEFORE this chunk was processed
    bool thinking_started = thinking_block_started;
    bool text_started     = text_block_started;

    for (const auto & diff : oaicompat_msg_diffs) {
        // handle thinking/reasoning content
        if (!diff.reasoning_content_delta.empty()) {
            if (!thinking_started) {
                events.push_back({
                    {"event", "content_block_start"},
                    {"data", {
                        {"type", "content_block_start"},
                        {"index", thinking_block_index},
                        {"content_block", {
                            {"type", "thinking"},
                            {"thinking", ""}
                        }}
                    }}
                });
                thinking_started = true;
            }

            events.push_back({
                {"event", "content_block_delta"},
                {"data", {
                    {"type", "content_block_delta"},
                    {"index", thinking_block_index},
                    {"delta", {
                        {"type", "thinking_delta"},
                        {"thinking", diff.reasoning_content_delta}
                    }}
                }}
            });
        }

        // handle regular text content
        if (!diff.content_delta.empty()) {
            if (!text_started) {
                events.push_back({
                    {"event", "content_block_start"},
                    {"data", {
                        {"type", "content_block_start"},
                        {"index", text_block_index},
                        {"content_block", {
                            {"type", "text"},
                            {"text", ""}
                        }}
                    }}
                });
                text_started = true;
            }

            events.push_back({
                {"event", "content_block_delta"},
                {"data", {
                    {"type", "content_block_delta"},
                    {"index", text_block_index},
                    {"delta", {
                        {"type", "text_delta"},
                        {"text", diff.content_delta}
                    }}
                }}
            });
        }

        // handle tool calls
        if (diff.tool_call_index != std::string::npos) {
            // use anthropic_has_reasoning for thinking block count (persists across calls)
            size_t content_block_index = (anthropic_has_reasoning ? 1 : 0) + (text_started ? 1 : 0) + diff.tool_call_index;

            if (!diff.tool_call_delta.name.empty()) {
                events.push_back({
                    {"event", "content_block_start"},
                    {"data", {
                        {"type", "content_block_start"},
                        {"index", content_block_index},
                        {"content_block", {
                            {"type", "tool_use"},
                            {"id", diff.tool_call_delta.id},
                            {"name", diff.tool_call_delta.name}
                        }}
                    }}
                });
            }

            if (!diff.tool_call_delta.arguments.empty()) {
                events.push_back({
                    {"event", "content_block_delta"},
                    {"data", {
                        {"type", "content_block_delta"},
                        {"index", content_block_index},
                        {"delta", {
                            {"type", "input_json_delta"},
                            {"partial_json", diff.tool_call_delta.arguments}
                        }}
                    }}
                });
            }
        }
    }

    return events;
}

//
// server_task_result_embd
//
json server_task_result_embd::to_json() {
    return res_type == TASK_RESPONSE_TYPE_OAI_EMBD
        ? to_json_oaicompat()
        : to_json_non_oaicompat();
}

json server_task_result_embd::to_json_non_oaicompat() {
    return json {
        {"index",     index},
        {"embedding", embedding},
    };
}

json server_task_result_embd::to_json_oaicompat() {
    return json {
        {"index",            index},
        {"embedding",        embedding[0]},
        {"tokens_evaluated", n_tokens},
    };
}

//
// server_task_result_rerank
//
json server_task_result_rerank::to_json() {
    return json {
        {"index",            index},
        {"score",            score},
        {"tokens_evaluated", n_tokens},
    };
}

//
// server_task_result_error
//
json server_task_result_error::to_json() {
    json res = format_error_response(err_msg, err_type);
    if (err_type == ERROR_TYPE_EXCEED_CONTEXT_SIZE) {
        res["n_prompt_tokens"] = n_prompt_tokens;
        res["n_ctx"]           = n_ctx;
    }
    return res;
}

//
// server_task_result_metrics
//
json server_task_result_metrics::to_json() {
    return json {
        { "idle",                            n_idle_slots },
        { "processing",                      n_processing_slots },
        { "deferred",                        n_tasks_deferred },
        { "t_start",                         t_start },

        { "n_prompt_tokens_processed_total", n_prompt_tokens_processed_total },
        { "t_tokens_generation_total",       t_tokens_generation_total },
        { "n_tokens_predicted_total",        n_tokens_predicted_total },
        { "t_prompt_processing_total",       t_prompt_processing_total },

        { "n_tokens_max",                    n_tokens_max },

        { "n_prompt_tokens_processed",       n_prompt_tokens_processed },
        { "t_prompt_processing",             t_prompt_processing },
        { "n_tokens_predicted",              n_tokens_predicted },
        { "t_tokens_generation",             t_tokens_generation },

        { "n_decode_total",                  n_decode_total },
        { "n_busy_slots_total",              n_busy_slots_total },

        { "slots",                           slots_data },
    };
}

//
// server_task_result_slot_save_load
//
json server_task_result_slot_save_load::to_json() {
    if (is_save) {
        return json {
            { "id_slot",   id_slot },
            { "filename",  filename },
            { "n_saved",   n_tokens },
            { "n_written", n_bytes },
            { "timings", {
                { "save_ms", t_ms }
            }},
        };
    }

    return json {
        { "id_slot",    id_slot },
        { "filename",   filename },
        { "n_restored", n_tokens },
        { "n_read",     n_bytes },
        { "timings", {
            { "restore_ms", t_ms }
        }},
    };
}

//
// server_task_result_slot_erase
//
json server_task_result_slot_erase::to_json() {
    return json {
        { "id_slot",  id_slot },
        { "n_erased", n_erased },
    };
}

//
// server_task_result_get_lora
//

json server_task_result_get_lora::to_json() {
    json result = json::array();
    for (size_t i = 0; i < loras.size(); ++i) {
        auto & lora = loras[i];
        json entry = {
            {"id",            i},
            {"path",          lora.info.path},
            {"scale",         lora.info.scale},
            {"task_name",     lora.info.task_name},
            {"prompt_prefix", lora.info.prompt_prefix},
        };
        if (!lora.alora_invocation_tokens.empty()) {
            entry["alora_invocation_string"] = lora.alora_invocation_string;
            entry["alora_invocation_tokens"] = lora.alora_invocation_tokens;
        }
        result.push_back(std::move(entry));
    }
    return result;
}

//
// server_task_result_apply_lora
//

json server_task_result_apply_lora::to_json() {
    return json {{ "success", true }};
}

//
// server_prompt_cache
//
size_t server_prompt_cache::size() const {
    size_t res = 0;

    for (const auto & state : states) {
        res += state.size();
    }

    return res;
}

size_t server_prompt_cache::n_tokens() const {
    size_t res = 0;

    for (const auto & state : states) {
        res += state.n_tokens();
    }

    return res;
}

server_prompt * server_prompt_cache::alloc(const server_prompt & prompt, size_t state_size_tgt, size_t state_size_dft) {
    // first check if the current state is contained fully in the cache
    for (auto it = states.begin(); it != states.end(); ++it) {
        const int cur_lcp_len = it->tokens.get_common_prefix(prompt.tokens);

        if (cur_lcp_len == (int) prompt.tokens.size()) {
            SRV_TRC("%s", " - prompt is already in the cache, skipping\n");
            return nullptr;
        }
    }

    // calculate checkpoints size to see if it will fit with the prompt
    size_t checkpoints_size = 0;
    for (const auto & ckpt : prompt.checkpoints) {
        checkpoints_size += ckpt.size();
    }

    const size_t state_size_new = state_size_tgt + state_size_dft + checkpoints_size;

    // skip over-limit entries to avoid disturbing the cache
    if (limit_size > 0 && state_size_new > limit_size) {
        SRV_WRN(" - prompt state size %.3f MiB exceeds cache size limit %.3f MiB, skipping\n",
                state_size_new / (1024.0 * 1024.0), limit_size / (1024.0 * 1024.0));
        return nullptr;
    }

    // remove any cached prompts that are fully contained in the current prompt
    for (auto it = states.begin(); it != states.end();) {
        const int len = it->tokens.get_common_prefix(prompt.tokens);

        if (len == (int) it->tokens.size()) {
            SRV_TRC(" - removing obsolete cached prompt with length %d\n", len);

            it = states.erase(it);
        } else {
            ++it;
        }
    }

    if (limit_size > 0) {
        // make room before allocating the new vectors to avoid breaching the limit
        while (!states.empty() && size() + state_size_new > limit_size) {
            SRV_WRN(" - making room for prompt cache entry, removing oldest entry (size = %.3f MiB)\n",
                    states.front().size() / (1024.0 * 1024.0));

            states.pop_front();
        }
    }

    std::vector<uint8_t> state_data_tgt;
    std::vector<uint8_t> state_data_dft;

    // check if we can allocate enough memory for the new state
    try {
        state_data_tgt.resize(state_size_tgt);
        state_data_dft.resize(state_size_dft);
    } catch (const std::bad_alloc & e) {
        SRV_ERR("failed to allocate memory for prompt cache state: %s\n", e.what());

        limit_size = std::max<size_t>(1, 0.4*size());

        SRV_WRN(" - cache size limit reduced to %.3f MiB\n", limit_size / (1024.0 * 1024.0));

        update();

        return nullptr;
    }

    states.push_back({
        /*.tokens      =*/ prompt.tokens.clone(),
        /*.data        =*/ {
            /*.main =*/ std::move(state_data_tgt),
            /*.drft =*/ std::move(state_data_dft),
        },
        /*.checkpoints =*/ prompt.checkpoints,
    });

    return &states.back();
}

bool server_prompt_cache::load(server_prompt & prompt, const server_tokens & tokens_new, llama_context * ctx_tgt, llama_context * ctx_dft, int32_t id_slot) {
    const int lcp_best = prompt.tokens.get_common_prefix(tokens_new);

    float f_keep_best = prompt.tokens.size() > 0 ? float(lcp_best) / prompt.tokens.size() : -1.0f; // empty slot: any cache entry wins
    float sim_best    = float(lcp_best) / tokens_new.size();

    SRV_TRC(" - looking for better prompt, base f_keep = %.3f, sim = %.3f\n", f_keep_best, sim_best);

    auto it_best = states.end();

    // find the most similar cached prompt, that would also preserve the most context
    for (auto it = states.begin(); it != states.end(); ++it) {
        const int lcp_cur = it->tokens.get_common_prefix(tokens_new);

        const float f_keep_cur = float(lcp_cur) / it->tokens.size();
        const float sim_cur    = float(lcp_cur) / tokens_new.size();

        // don't trash large prompts
        if (f_keep_cur < 0.25f) {
            continue;
        }

        if (f_keep_best < f_keep_cur && sim_best < sim_cur) {
            f_keep_best = f_keep_cur;
            sim_best    = sim_cur;

            it_best = it;
        }
    }

    if (it_best != states.end()) {
        SRV_TRC(" - found better prompt with f_keep = %.3f, sim = %.3f\n", f_keep_best, sim_best);

        {
            auto & data = it_best->data.main;

            const size_t size = data.size();
            const size_t n = llama_state_seq_set_data_ext(ctx_tgt, data.data(), size, id_slot, 0);
            if (n != size) {
                SRV_ERR("failed to restore state with size %zu\n", size);

                return false;
            }

            data.clear();
            data.shrink_to_fit();
        }

        {
            auto & data = it_best->data.drft;

            if (!data.empty()) {
                GGML_ASSERT(ctx_dft);

                const size_t size = data.size();
                const size_t n = llama_state_seq_set_data_ext(ctx_dft, data.data(), size, id_slot, 0);
                if (n != size) {
                    SRV_WRN("failed to restore state with size %zu\n", size);

                    return false;
                }

                data.clear();
                data.shrink_to_fit();
            }
        }

        prompt = std::move(*it_best);

        states.erase(it_best);
    }

    return true;
}

void server_prompt_cache::update() {
    if (limit_size > 0) {
        while (!states.empty() && size() > limit_size) {
            SRV_WRN(" - cache size limit reached, removing oldest entry (size = %.3f MiB)\n", states.front().size() / (1024.0 * 1024.0));

            states.pop_front();
        }
    }

    // average size per token
    const float size_per_token = std::max<float>(1.0f, float(size()) / (std::max<size_t>(1, n_tokens())));

    // dynamically increase the token limit if it can fit in the memory limit
    const size_t limit_tokens_cur = limit_size > 0 ? std::max<size_t>(limit_tokens, limit_size/size_per_token) : limit_tokens;

    if (limit_tokens > 0) {
        while (!states.empty() && n_tokens() > limit_tokens_cur) {
            SRV_WRN(" - cache token limit (%zu, est: %zu) reached, removing oldest entry (size = %.3f MiB)\n",
                    limit_tokens, limit_tokens_cur, states.front().size() / (1024.0 * 1024.0));

            states.pop_front();
        }
    }

    SRV_TRC(" - cache state: %zu prompts, %.3f MiB (limits: %.3f MiB, %zu tokens, %zu est)\n",
            states.size(), size() / (1024.0 * 1024.0), limit_size / (1024.0 * 1024.0), limit_tokens, limit_tokens_cur);

    for (const auto & state : states) {
        SRV_TRC("   - prompt %p: %7d tokens, checkpoints: %2zu, %9.3f MiB\n",
                (const void *)&state, state.n_tokens(), state.checkpoints.size(), state.size() / (1024.0 * 1024.0));
    }
}
