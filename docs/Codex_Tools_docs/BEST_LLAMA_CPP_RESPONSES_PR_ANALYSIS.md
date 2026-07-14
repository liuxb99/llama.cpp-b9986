# Best llama.cpp Responses / Codex Bridge PR Analysis

> **Analyzed at**: `72b020c47` (HEAD of `liuxb99/llama.cpp-b9986`)
> **Upstream target**: `ggml-org/llama.cpp`
> **Codex dump hashes**: `658ff3c4` (Compact, 12 tools) + `be3d48ac` (Resolved, 259 tools)
> **Date**: 2026-07-14

---

## 1. Candidate PR Inventory

Searched `ggml-org/llama.cpp` for all PRs/issues touching Responses API, Codex CLI, tool calling, streaming SSE, namespace/web_search tools.

| # | Title | Author | State | Branch | Latest Commit | Focus |
|---|-------|--------|-------|--------|---------------|-------|
| **21174** | server: improve Responses API compliance and Codex CLI compatibility | @krystophny | OPEN | `responses-api-codex-compat` | `8f8009edc` | Request conversion, streaming events, skipping non-function tools |
| **23041** | Support for Codex CLI by skipping unsupported Responses tools | @SidShaytay | **MERGED** | `master` | N/A | Minimal — skip non-function tool types instead of 400 |
| **24295** | Responses API: silently drops 'namespace' and 'web_search' tool types (issue) | @kxy56618573 | OPEN | — | — | Reports namespace/web_search gap with suggested fix |
| **12379** | chat: parser rework for streaming tool diffs + partial JSON + all formats | @ochafik | OPEN | `tool-diffs` | N/A | Full parser rewrite; not Responses-specific |
| **20708** | chat: new parser should not crash inference | @jpohhhh | **MERGED** | `master` | N/A | Parser robustness (crash on RESULT_FAIL) |
| **19765** | common: merge qwen3-coder and nemotron nano 3 parsers | @aldehir | **MERGED** | `master` | N/A | Parser merge, parallel tool calling |
| **18655** | WebUI + MCP Agentic Loop | @ServeurpersoCom | **MERGED** | `master` | N/A | UI, MCP agentic loop (not Responses bridge) |
| **14702** | Misc. bug: OpenAI API v1/responses llama-server (issue) | @foxbg | OPEN | — | — | Original feature request for Responses API |
| **19720** | (prior art for #21174) | @riskywindow | Stale | — | — | 500+ commits behind, superseded by #21174 |
| **23226** | server + ui: SSE Replay Buffer | @ServeurpersoCom | **MERGED** | `master` | N/A | SSE replay, not Responses-specific |
| **23994** | server: add SSE keepalive | @joleuger | OPEN | N/A | N/A | SSE keepalive, not Responses-specific |

### Relevant but Not Responses-Specific

- **#12379** (ochafik): Major parser rewrite (`chat-parser.cpp`), partial JSON/streaming, all model tool-call formats. Not targeted at Responses API but could eventually replace the downstream normalizer.

---

## 2. Completeness Score Table

Scored 0–3 per criterion (0 = absent, 3 = complete). Sources evaluated: PR source code, PR tests, Codex dump, current b9986 source.

| Criterion | #21174 | #23041 | b9986 | Notes |
|-----------|--------|--------|-------|-------|
| Responses request → Chat Completions conversion | **3** | 1 | 0 | #21174 handles input items, tool output history, instructions |
| `function` tool type in request | **3** | 2 | 0 | #21174 has `responses_tool_to_chatcmpl_tool()`; b9986 relies on upstream |
| `custom` tool type in request | 0 | 0 | 0 | Neither PR nor b9986 handles custom tool conversion |
| `namespace` tool type in request | 0 (skipped) | 0 (skipped) | 0 | #21174 explicitly skips non-function types; b9986 also skips |
| `web_search` tool type in request | 0 (skipped) | 0 (skipped) | 0 | Both skip — #24295 reports this as a bug |
| `tool_search` tool type in request | 0 | 0 | 0 | Not handled anywhere upstream |
| Streaming SSE: `sequence_number` | **3** | 0 | 0 | #21174 adds to ALL events |
| Streaming SSE: `output_index` | **3** | 0 | 0 | #21174 adds to all output item events |
| Streaming SSE: `content_index` | **3** | 0 | 0 | #21174 adds to content-related events |
| Streaming SSE: `output_item.added` | **3** | 0 | 0 | #21174 populates full item |
| Streaming SSE: `function_call_arguments.delta` | **3** | 0 | **1** | b9986 has basic chat-msg diffing through PEG parser; #21174 has explicit Responses-level streaming |
| Streaming SSE: `output_item.done` | **3** | 0 | 0 | #21174 emits done events |
| Streaming SSE: `response.completed` | **3** | 0 | 0 | #21174 emits completed event |
| `call_id` / `item_id` / `status` field mapping | **3** | 0 | **2** | #21174 has explicit Responses ID mapping; b9986 has output restoration but no streaming |
| Tool output history conversion | **3** | 0 | 0 | #21174 has `append_tool_output_message()` and `encode_tool_output_content()` |
| Developer/system role merging | **3** | 0 | 0 | #21174 merges system/developer messages for Qwen compatibility |
| Input file support | **3** | 0 | 0 | #21174 handles `input_file` with data and filename-only modes |
| Reasoning content handling | **3** | 0 | 0 | #21174 handles reasoning content as array/string/null/omitted |
| **Output side**: `responses_tool_map` | 0 | 0 | **3** | b9986 has `build_responses_tool_map()` — 5 types supported |
| **Output side**: `restore_tool_call_from_original()` | 0 | 0 | **3** | b9986 restores function/custom/namespace/tool_search/web_search |
| **Output side**: XML fallback parser | 0 | 0 | **3** | b9986 has `parse_xml_tool_call_fallback()` |
| **Output side**: `<invoke>` parser | 0 | 0 | **3** | b9986 has `parse_invoke_block()` |
| **Output side**: `resolve_tool_name()` | 0 | 0 | **3** | b9986 has 4-step deterministic name resolution |
| **Output side**: `parse_normalized_tool_calls()` | 0 | 0 | **3** | b9986 has unified normalizer |
| Namespace flattening | 0 | 0 | 0 | Neither does this — #24295's suggested fix exists as comment only |
| Codex CLI E2E verified | **3** | 0 | 0 | #21174 has `codex exec -p local` tests passing |
| Incomplete tool call safety | 0 | 0 | **2** | b9986's normalizer handles partial blocks; #21174 defers to upstream PEG parser |
| Tools NOT injected into prompt | 0 | 0 | **3** | b9986 explicitly sets `tool_schema=0`, `grammar=0` |
| **Total** | **43** | **3** | **29** | |

---

## 3. Most Complete PR: #21174

### Conclusion

**PR #21174 (`responses-api-codex-compat`)** is the best upstream baseline for llama.cpp Responses API / Codex CLI compatibility.

- **PR #**: `21174`
- **Author**: @krystophny
- **Branch**: `responses-api-codex-compat` (on fork `krystophny/llama.cpp`)
- **Latest commit**: `8f8009edc9a6bf5e939ea737af52ec403999983b`
- **Base**: `ggml-org/llama.cpp` `master`
- **Status**: OPEN (not merged)

### Why #21174 is most complete

1. **Only PR with comprehensive Responses request→Chat Completions conversion** — handles input text, images, files, tool outputs, reasoning items, compaction summaries
2. **Only PR with proper streaming SSE events** — `sequence_number`, `output_index`, `content_index`, `output_item.added/done`, `function_call_arguments.delta`, `response.completed`
3. **Only PR verified with Codex CLI E2E** — `codex exec -p local` tests pass for both text and tool calling
4. **Only PR with 14 pytest tests** covering all code paths
5. **Most extensive diff** — 415 additions, 99 deletions in `server-chat.cpp` alone

### Why #21174 is NOT the complete solution

The PR is **strictly about the input side** (Responses request → Chat Completions conversion, streaming output). It does **NOT** handle the **output side** (model tool calls → Responses format). Specifically:

- No `responses_tool_map` (reverse mapping from tools → Responses format)
- No `restore_tool_call_from_original()` (output restoration)
- No `parse_xml_tool_call_fallback()` (XML format parser)
- No `resolve_tool_name()` (deterministic name resolution)
- No namespace flattening for `type: "namespace"` tools
- No `custom` type tool support
- Skips non-function tool types instead of converting them

---

## 4. PR #21174 vs Current b9986: Complementary Architectures

```
PR #21174:  INPUT side (Responses → ChatCompletions)
┌─────────────────────────────────────┐
│ server_chat_convert_responses_to_   │
│ chatcmpl()                           │
│  ├── input items → messages          │
│  ├── instructions → system msg       │
│  ├── tool output history → tool msgs │
│  ├── tools[] → chatcmpl tools[]      │
│  └── streaming SSE events            │
└─────────────────────────────────────┘

Current b9986: OUTPUT side (tool calls → Responses format)
┌─────────────────────────────────────┐
│ build_responses_tool_map()           │
│ parse_normalized_tool_calls()        │
│  ├── parse_xml_tool_call_fallback()  │
│  ├── parse_invoke_block()            │
│  └── resolve_tool_name()             │
│ restore_responses_tool_call()        │
│  └── restore_tool_call_from_original│
└─────────────────────────────────────┘
```

Both are needed for a complete Codex CLI bridge.

---

## 5. Three-Way Differential Matrix

### 5.1 `function` tools

| Dimension | Codex dump | #21174 | b9986 |
|-----------|-----------|--------|-------|
| Request schema | `{type:"function", name, description, parameters, strict}` | `responses_tool_to_chatcmpl_tool()` converts to chatcmpl function | Relies on upstream conversion (no custom function) |
| Internal representation | — | Chat Completions function format | `NormalizedToolCall` struct |
| Output restoration | Codex expects `function_call` output item | **MISSING** — no restore layer | `restore_tool_call_from_original(type="function")` → `function_call` |
| Streaming | Codex expects `function_call_arguments.delta` | **Has** with consistent call_id | Basic chat-msg diff, no Responses-level streaming |

**b9986 vs #21174**: Complementary. #21174 handles the request side, b9986 handles the output restoration side.  
**Missing**: #21174's streaming events need to be integrated with b9986's output restoration.

### 5.2 `custom` tools

| Dimension | Codex dump | #21174 | b9986 |
|-----------|-----------|--------|-------|
| Request schema | `{type:"custom", name, description, format:{type:"grammar", syntax:"lark", definition:"..."}}` | **SKIPPED** (non-function → 400 or skip) | `build_responses_tool_map()` stores custom tools but **skips** in `responses_tool_to_chatcmpl_tools()` |
| Internal representation | — | N/A | Map key = `custom_tool_` + sanitized name; `input` field instead of `arguments` |
| Output restoration | Codex expects `custom_tool_call` output item | **MISSING** | `restore_tool_call_from_original(type="custom")` → `custom_tool_call` with `input` field |

**Issue**: Neither #21174 nor b9986 converts custom tools to chatcmpl format. The model never sees custom tools during generation. Only the **output side** restoration is handled (b9986 can restore `custom_tool_call` output items from completed calls).

### 5.3 `namespace` tools

| Dimension | Codex dump | #21174 | b9986 |
|-----------|-----------|--------|-------|
| Request schema | `{type:"namespace", name, description, tools:[{type:"function", name, ...}]}` | **SKIPPED** (non-function) | **SKIPPED** (non-function) |
| Number in dump | 1 (compact) / 17 (resolved, 246 sub-tools) | All dropped | All dropped |
| Impact | All MCP connector tools invisible to model | **BROKEN** | **BROKEN** |
| Suggested fix | Flatten namespace sub-tools into top-level functions | Not implemented | Not implemented |

**P0 gap**: Namespace flattening is the #1 blocker for Codex CLI compatibility. Without it, all MCP tools (github, canva, figma, supabase, vercel, etc.) are invisible.

### 5.4 `tool_search` tools

| Dimension | Codex dump | #21174 | b9986 |
|-----------|-----------|--------|-------|
| Request schema | `{type:"tool_search", execution:"client", description, parameters}` | **SKIPPED** (non-function) | `build_responses_tool_map()` stores it; **skipped** in conversion |
| Output handling | Codex expects `tool_search_call` output item | **MISSING** | `parse_normalized_tool_calls()` has `<tool_search>` format recognition; `restore_tool_call_from_original(type="tool_search")` creates `tool_search_call` |

**Status**: b9986 has the output restoration path for tool_search, but it's never triggered because the model never sees a tool_search tool definition. The `<tool_search>` parser in `parse_normalized_tool_calls()` is speculative (Format D from analysis doc §7) — no real model output of this format has been observed.

### 5.5 `web_search` tools

| Dimension | Codex dump | #21174 | b9986 |
|-----------|-----------|--------|-------|
| Request schema | `{type:"web_search", external_web_access:true, parameters:{query:{...}}}` | **SKIPPED** (non-function) | `build_responses_tool_map()` stores it; **skipped** in conversion |
| Output restoration | Codex expects `web_search_call` output item | **MISSING** | `restore_tool_call_from_original(type="web_search")` creates `web_search_call` |

---

## 6. Current b9986 Self-Made Logic Inventory

Every function in b9986 that is custom (not from any upstream PR):

| Function | File | Lines | What it does | Why added | Assessment |
|----------|------|-------|-------------|-----------|------------|
| `build_responses_tool_map()` | `server-chat.cpp` | ~150 | Builds reverse map from tools[] → Responses format info | Need map to restore output format | **KEEP** — core bridge logic |
| `responses_tool_to_chatcmpl_tools()` | `server-chat.cpp` | ~130 | Converts Responses tools[] → chatcmpl tools[] | Needed when tools injected (now unused) | **KEEP** (but call sites removed) |
| `parse_xml_tool_call_fallback()` | `server-chat.cpp` | ~260 | Parses `<tool_call>`, `<invoke>` from model output | Needed when tools not injected into prompt | **KEEP** — core parser |
| `parse_invoke_block()` | `server-chat.cpp` | ~50 | Helper for `<invoke name=X>` format | Supports Claude-style tool calls | **KEEP** — part of parser |
| `parse_normalized_tool_calls()` | `server-chat.cpp` | ~100 | Unified normalizer entry point | Wraps fallback parser + tool_map resolution | **KEEP** — core bridge logic |
| `resolve_tool_name()` | `server-chat.cpp` | ~60 | 4-step deterministic tool name resolution | Needed when map present | **KEEP** — core bridge logic |
| `normalized_calls_to_chat_msg()` | `server-chat.cpp` | ~30 | Convert NormalizedToolCall vector → chat msg | Bridges parser output → chat format | **KEEP** — core bridge logic |
| `restore_tool_call_from_original()` | `server-task.cpp` | ~100 | Restore normalized call → Responses output item | Maps back to original tool type/names | **KEEP** — core bridge logic |
| `restore_responses_tool_call()` | `server-task.cpp` | ~110 | Entry point for output restoration | Wraps restore with map lookup | **KEEP** — core bridge logic |
| `sanitize_tool_name()` | `server-chat.cpp` | ~20 | Sanitize tool name for JSON key | Used everywhere | **KEEP** — stable utility |

### Heuristic Parser Rules — Critical Review

| Rule | File | Assessment | Action |
|------|------|-----------|--------|
| `<tool_call>name{json}</tool_call>` detection | `parse_xml_tool_call_fallback` | Valid model output format (observed) | **KEEP** |
| `<tool_call>{json envelope}</tool_call>` fallback | `parse_xml_tool_call_fallback` | JSON envelope has `name`+`arguments` keys | **KEEP** — standard format |
| `<invoke name=X><parameter name=k>v</parameter></invoke>` | `parse_invoke_block` | Claude-style format (observed) | **KEEP** |
| `<tool_search>query</tool_search>` | `parse_normalized_tool_calls` | Speculative — never observed in real model output | **KEEP** (harmless, well-defined) |
| `{"name":"...","arguments":{...}}` JSON fallback | `parse_xml_tool_call_fallback` | Standard JSON tool call | **KEEP** |

**None of the current parser rules should be deleted.** They are all well-defined, correct implementations of documented formats. The risk is adding MORE heuristic rules (e.g., guessing fenced code blocks inside `<tool_call>`).

---

## 7. Functions to Keep, Delete, or Replace

### KEEP (essential bridge logic)

| Function | Reason |
|----------|--------|
| `build_responses_tool_map()` | Core mapping for output restoration |
| `parse_xml_tool_call_fallback()` | Model output parser when tools not injected |
| `parse_invoke_block()` | Part of XML parser |
| `parse_normalized_tool_calls()` | Unified normalizer entry point |
| `resolve_tool_name()` | Deterministic name resolution |
| `normalized_calls_to_chat_msg()` | Bridge to chat format |
| `restore_tool_call_from_original()` | Output restoration for all 5 types |
| `restore_responses_tool_call()` | Entry point for restoration |
| `sanitize_tool_name()` | Utility |
| Context diagnostic helpers | Debugging |

### REPLACE (adopt from #21174)

| b9986 function | #21174 replacement | Why |
|---------------|-------------------|-----|
| `server_chat_convert_responses_to_chatcmpl()` (current minimal version) | #21174's full version | #21174 has input items, tool output history, developer role merging, input_file, reasoning content |
| Streaming SSE (basic chat-msg diff) | #21174's `server_task_result_responses` | #21174 has proper Responses-level streaming events |
| `__responses_tool_map` storage in body | #21174's approach | Both store it; merge approaches |

### DELETE (no longer needed)

| Function | Reason |
|----------|--------|
| `responses_tool_to_chatcmpl_tools()` call sites | Already removed — tools not injected |

### ADD (from #21174, not in b9986)

| Function | File | Purpose |
|----------|------|---------|
| `responses_tool_to_chatcmpl_tool()` (single tool converter) | `server-chat.cpp` | Convert one Responses tool to chatcmpl format |
| `encode_tool_output_content()` | `server-chat.cpp` | Convert tool output items to chat content |
| `append_tool_output_message()` | `server-chat.cpp` | Append tool output as tool message in history |
| `input_file_text()` | `server-chat.cpp` | Convert input_file items to text |
| Developer/system role merging | `server-chat.cpp` | Merge for templates requiring system at position 0 |
| Reasoning content handling | `server-chat.cpp` | Handle 4 formats of reasoning content |
| SSE streaming with sequence/indices | `server-task.cpp` | Proper Responses-level streaming events |

---

## 8. Priority Fix Plan

### P0 — Blocking Codex CLI tool calling

| # | Fix | PR Commit | Files | Function | Codex Dump Evidence | Backport Order |
|---|-----|-----------|-------|----------|--------------------|-----------------|
| 1 | **Namespace flattening** in `responses_tool_to_chatcmpl_tool()` | #24295 (suggested fix, not merged) | `server-chat.cpp` | `responses_tool_to_chatcmpl_tool()` | 17 namespaces, 246 sub-tools in resolved profile | 1st |
| 2 | **Integrate #21174's request conversion** | `8f8009ed` | `server-chat.cpp` | `server_chat_convert_responses_to_chatcmpl()` | All 12 tools rely on proper input conversion | 2nd |
| 3 | **Integrate #21174's streaming SSE** | `8f8009ed` | `server-task.cpp`, `server-task.h` | `task_result_state`, SSE events with indices | Codex needs `output_item.added/done`, `arguments.delta` | 3rd |

### P1 — Important for complete compliance

| # | Fix | PR/Fix | Files | Reason |
|---|-----|--------|-------|--------|
| 4 | web_search as function conversion | #24295 suggestion | `server-chat.cpp` | Codex resolved profile includes web_search |
| 5 | Developer/system role merging | #21174 | `server-chat.cpp` | Qwen template compatibility |
| 6 | Input file support | #21174 | `server-chat.cpp` | Codex sends input_file items |
| 7 | Reasoning content in history | #21174 | `server-chat.cpp` | Codex includes reasoning in multi-turn |

### P2 — Nice to have

| # | Fix | Reason |
|---|------|--------|
| 8 | custom tool request conversion | Rare (only apply_patch in compact profile) |
| 9 | tool_search output integration | speculative format, no observed output |
| 10 | Incomplete tool call streaming edge cases | Current normalizer handles basic partials |

---

## 9. P0 Backport Detail

### P0-1: Namespace Flattening

**Evidence from Codex dump (be3d48ac)**:
```
17 namespaces, 246 sub-tools, ALL type="function" inside namespace.tools[]
Connectors: mcp__codex_apps__github (89 tools), supabase (29), vercel (24), etc.
```

**Current behavior**: Both #21174 and b9986 skip `type: "namespace"` → all MCP tools invisible.

**Suggested fix** (from #24295, confirmed by dump analysis):
```cpp
if (tool_type == "namespace") {
    if (resp_tool.contains("tools") && resp_tool.at("tools").is_array()) {
        for (json & inner_tool : resp_tool.at("tools")) {
            if (json_value(inner_tool, "type", std::string()) == "function") {
                json chatcmpl_tool;
                inner_tool.erase("type");
                if (!inner_tool.contains("strict")) inner_tool["strict"] = true;
                chatcmpl_tool["type"] = "function";
                chatcmpl_tool["function"] = inner_tool;
                chatcmpl_tools.push_back(chatcmpl_tool);
            }
        }
    }
    continue;
}
```

**Map key consideration**: Sub-tool names inside namespace have `original_name` that may collide across namespaces (5 known conflicts from dump). `build_responses_tool_map()` already handles this via `{namespace}__{subtool}` composite key. For the forward direction (request→chat), the flattened tool name should be the **composite key** so the model can output `mcp__codex_apps__github__get_issue` and the map matches.

### P0-2: Request Conversion from #21174

**Key additions needed**:
- Input item handling (text, image, file, reasoning)
- Tool output history → tool messages
- `instructions` → system message with developer role merging
- Reasoning content in multi-turn history
- Responses request key stripping (store, include, etc.)

### P0-3: Streaming SSE from #21174

**Key additions needed**:
- `server_task_result_responses` result type (already exists in b9986? check)
- `task_result_state` with counters for output_index, content_index, sequence_number
- `output_item.added` events with full item body
- `function_call_arguments.delta` events with consistent item_id
- `output_item.done` events
- `response.completed` event

---

## 10. Engineering Rule: No Parser Guessing

### Prohibited

DO NOT add heuristic fallbacks for unrecognized model output formats:

1. ❌ Guessing tool name from fenced code blocks inside `<tool_call>`
2. ❌ Extracting tool name from arbitrary text prefixes
3. ❌ "Fixing" anonymous blocks by inserting default names
4. ❌ Adding regex-based guessers for unobserved formats
5. ❌ Accepting output that matches NO tool_map entry (use tool_map as the sole source of truth)

### Allowed

1. ✅ Well-defined XML parser for `<tool_call>name{json}</tool_call>` (Format A)
2. ✅ Well-defined XML parser for `<invoke name="x"><parameter name="k">v</parameter></invoke>` (Format B)
3. ✅ Well-defined JSON parser for `{"name":"...","arguments":{...}}` (Format C)
4. ✅ Well-defined `<tool_search>...</tool_search>` parser (Format D, speculative but harmless)
5. ✅ Tool_map validation — reject calls whose name doesn't match any entry
6. ✅ Deterministic name resolution (4-step priority: exact→sanitized→namespace→unique_name)

### Rationale

The model generates text in formats it learned during training. The parser should only recognize formats that:

1. Are **documented** in the model's training data
2. Have been **observed** in real output
3. Can be validated against a **known tool schema** (the `responses_tool_map`)

Adding heuristics for unobserved formats introduces:
- False positives (valid content interpreted as tool calls)
- Maintenance burden
- Non-deterministic behavior
- Security surface (unvalidated input interpreted as tool dispatch)

---

## Appendix A: File Mapping

| Area | #21174 File | b9986 File | Action |
|------|------------|-----------|--------|
| Request conversion | `server-chat.cpp` (responses_tool_to_chatcmpl_tool, input→msg conversion, tool output history) | `server-chat.cpp` (minimal version) | **Replace** b9986's minimal version with #21174's |
| Output restoration | — | `server-task.cpp` (restore_tool_call_from_original) | **Keep** b9986's version |
| Tool map building | — | `server-chat.cpp` (build_responses_tool_map) | **Keep** b9986's version |
| XML parser | — | `server-chat.cpp` (parse_xml_tool_call_fallback) | **Keep** b9986's version |
| SSE streaming | `server-task.cpp` (task_result_state, response events) | `server-task.cpp` (basic loop) | **Replace** with #21174's structured events |
| Namespace handling | — | — | **Add** (not in either) |
| Tests | `test_compat_oai_responses.py` (14 tests) | — | **Add** from #21174 |

## Appendix B: Current b9986 HEAD

```
72b020c47 revert: remove tool output format hint
```

State verified:
- ✅ `build_responses_tool_map()` — present
- ✅ `parse_xml_tool_call_fallback()` — present
- ✅ `parse_invoke_block()` — present
- ✅ `parse_normalized_tool_calls()` — present
- ✅ `resolve_tool_name()` — present
- ✅ `restore_tool_call_from_original()` — present
- ✅ `restore_responses_tool_call()` — present
- ✅ `sanitize_tool_name()` — present
- ✅ `tool_schema=0`, `grammar=0` — set
- ✅ No format hint — removed
- ✅ Tools NOT injected into prompt
- ✅ `CODEX_TOOL_NORMALIZER_ANALYSIS.md` — preserved
- ✅ `ANONYMOUS_TOOL_CALL_ROOT_CAUSE.md` — preserved
- ✅ `CODEX_ACTUAL_TOOLS.json` / `.md` — preserved
