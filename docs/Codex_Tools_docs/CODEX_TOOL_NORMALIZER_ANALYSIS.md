# Codex Tool Normalizer — Analysis & Design

Based on two real captured Codex tool profiles:
- `658ff3c4` (Compact, 12 tools, ~59KB)
- `be3d48ac` (Resolved, 30→259 tools, ~1MB)

---

## 1. Two-Hash Comparison

| Metric | 658ff3c4 (Compact) | be3d48ac (Resolved) |
|--------|--------------------|---------------------|
| Top-level tools | 12 | 30 |
| Expanded total | 12 | 259 |
| function | 9 | 12 |
| custom | 1 (apply_patch) | 0 |
| namespace | 1 (image_gen) | 17 |
| namespace sub-tools | 1 | 246 |
| tool_search | 1 | 0 |
| web_search | 0 | 1 |
| Tool map entries | 12 | 248 |
| Name conflicts | 0 | 5 (cross-ns, acceptable) |

The two profiles represent different stages of Codex discovery:
- **Compact**: lightweight bootstrap, file editing via custom Lark grammar tool, deferred MCP discovery via `tool_search`
- **Resolved**: all connectors discovered and expanded inline, goal tracking tools added, web_search present

---

## 2. Common Tools (present in BOTH hashes)

These 9 fixed + 1 namespace are the stable core:

| Tool | Type | Map Key | Notes |
|------|------|---------|-------|
| `shell_command` | function | `shell_command` | |
| `list_mcp_resources` | function | `list_mcp_resources` | |
| `list_mcp_resource_templates` | function | `list_mcp_resource_templates` | |
| `read_mcp_resource` | function | `read_mcp_resource` | |
| `update_plan` | function | `update_plan` | |
| `request_user_input` | function | `request_user_input` | |
| `list_available_plugins_to_install` | function | `list_available_plugins_to_install` | |
| `request_plugin_install` | function | `request_plugin_install` | |
| `view_image` | function | `view_image` | |
| `image_gen` | namespace | `image_gen__imagegen` | same in both |

## 3. Unique Tools

### Compact-only (658ff3c4)
- `apply_patch` (custom, Lark grammar)
- `tool_search` (BM25-based deferred discovery)

### Resolved-only (be3d48ac)
- `get_goal` / `create_goal` / `update_goal` (function, goal tracking)
- `web_search` (type)
- `multi_agent_v1` namespace (5 sub-tools: close_agent, resume_agent, send_input, spawn_agent, wait_agent)
- `mcp__codex_apps__*` 10 connector namespaces (canva=32, figma=19, github=89, hotline=1, hugging_face=9, openai_platform=3, plugin_management=4, sites=19, supabase=29, vercel=24)
- `mcp__node_repl` namespace (3 sub-tools)
- `mcp__openai_api_key_local_confirmation` namespace (1)
- `mcp__sites_design_picker` namespace (1)
- `codex_app` namespace (3 sub-tools)

---

## 4. Map Key Rules (from real data)

### Map key = sanitize_tool_name(name)

`sanitize_tool_name()` behavior:
- Keeps alphanumeric, `_`, `-` only
- Replaces other chars with `_`
- **Strips leading underscores**
- Truncates to 64 chars

Consequences:
- Sub-tools with leading `_` lose the prefix (e.g., `_autofill_design` → `autofill_design`)
- Names with hash suffixes get truncated if > 64 chars total

### Key patterns by type

#### Top-level function
- `{original_name}` → `shell_command`, `update_plan`

#### Namespace sub-tool
- `{namespace_name}__{subtool_name}` → `multi_agent_v1__spawn_agent`, `mcp__codex_apps__github__get_issue`
- Double-underscore separator
- Sub-tool names have leading `_` stripped

#### Custom
- `{sanitized_name}` → `apply_patch`
- In original conversion code, custom tools get `custom_tool_` prefix, but `build_responses_tool_map()` omits this prefix and stores key = sanitized(name)

#### web_search
- map key = `web_search`

#### tool_search
- map key = `tool_search`

---

## 5. Name Collisions

5 cross-namespace `original_name` collisions found (all in be3d48ac):

| original_name | Namespaces |
|---------------|------------|
| `_create_branch` | github, supabase |
| `_fetch` | canva, github |
| `_get_project` | supabase, vercel |
| `_list_projects` | supabase, vercel |
| `_search` | canva, github |

All disambiguated by namespace prefix in map key. No top-level name conflicts.

**No duplicate original_name within a single namespace.** Sanctize-then-compare also clean.

---

## 6. Tool Type Internal Structure

### function
```json
{
  "type": "function",
  "name": "shell_command",
  "description": "...",
  "parameters": { "type": "object", "properties": {...}, "required": [...] }
}
```

### custom
```json
{
  "type": "custom",
  "name": "apply_patch",
  "description": "...",
  "format": { "type": "grammar", "syntax": "lark", "definition": "..." }
}
```

### namespace (top-level container)
```json
{
  "type": "namespace",
  "name": "mcp__codex_apps__github",
  "description": "...",
  "tools": [ { "type": "function", "name": "_get_issue", ... }, ... ]
}
```
All sub-tools are type `function` (observed).

### tool_search
```json
{
  "type": "tool_search",
  "execution": "client",
  "description": "...",
  "parameters": { "type": "object", "properties": { "query": {...}, "limit": {...} }, "required": ["query"] }
}
```

### web_search
```json
{
  "type": "web_search",
  "external_web_access": true,
  "parameters": { "type": "object", "properties": { "query": {...} }, "required": ["query"] }
}
```

---

## 7. Model Output Formats (observed in wild)

### Format A: `<tool_call>name{json}</tool_call>`
```
<tool_call>get_weather{"loc":"NYC"}</tool_call>
```
Multiple: `<tool_call>fn1{}fn2{}</tool_call>` (within one block) or `<tool_call>fn1{}</tool_call><tool_call>fn2{}</tool_call>` (separate).

### Format B: `<invoke name="name"><parameter name="k">v</parameter></invoke>`
```xml
<invoke name="get_weather">
<parameter name="loc">NYC</parameter>
</invoke>
```
Model tool call format used by some Claude-based chains.

### Format C: JSON `{"name":"...","arguments":{...}}`
```
{"name": "get_weather", "arguments": {"loc": "NYC"}}
```
Can also appear within a `<tool_call>` block as sub-format.

### Format D: `<tool_search>query{json}</tool_search>` (hypothetical based on tool_search type)
Detected in Compact profile when model calls the tool_search tool. Currently not handled and passes through as text.

---

## 8. Normalized Internal Representation

All formats above should normalize to:

```cpp
struct NormalizedToolCall {
    std::string name;                      // sanitized, map-key ready
    std::string arguments;                 // JSON string (always)
    std::string source_format;             // "tool_call" | "invoke" | "json" | "tool_search"
    bool        partial;                   // true if still streaming (incomplete blocks)
    bool        accepted;                  // false if rejected by tool_map validation
    std::string raw_text;                  // original text, for rejected calls to restore as content
};
```

---

## 9. Name → Map Lookup Order (strict priority)

```
1. Exact map key match in responses_tool_map
2. sanitize_tool_name(name) in responses_tool_map
3. Unique original_name match (exact, across entire map)
4. Namespace: {namespace_name}__{sanitized_name} composite key in map
5. If >1 match at any step → REJECT (ambiguous)
6. No match at all → REJECT (keep as content text)
```

### Ambiguity examples (from real data)
- `_create_branch` resolves to both `github__create_branch` and `supabase__create_branch` → REJECT (step 4 would give 2 matches)
- `get_project` resolves to both `supabase__get_project` and `vercel__get_project` → REJECT (step 4 would give 2 matches)

For these ambiguous cases, only step 1 (exact map key) or step 3 (unique original_name) can succeed. If the model outputs `<invoke name="get_project">` with no disambiguating namespace, it is rejected.

---

## 10. Full Pipeline Flow

```
Model output text
    │
    ▼
common_chat_parse (PEG parser)
    │ tools empty (no prompt injection)
    ▼
parse_xml_tool_call_fallback(raw_text, is_partial, gen_prompt, msg, tool_map)
    ├── Detect <tool_call> → parse name{json} (Format A/B/C)
    ├── Detect <invoke>    → parse name attr + <parameter> pairs (Format B)
    ├── Detect <tool_search> → parse as tool_search_call (Format D)
    │
    ├── For each detected call:
    │   ├── If tool_map is nullptr: ACCEPT (backward compat)
    │   ├── If name in tool_map via lookup order: ACCEPT
    │   └── else: REJECT → keep raw text as content
    │
    └── Produces common_chat_msg with tool_calls[] vector
    │
    ▼
update_chat_msg (server-task.cpp)
    ├── chat_msg = new_msg (with parsed tool_calls)
    ├── compute_diffs(msg_prv_copy, chat_msg)
    │   ├── content_delta = string_diff(old_content, new_content)
    │   └── For each tool call: emit diff (name once, args streamed)
    │
    ▼
server_task_result_cmpl_final/partial
    ├── output_done / function_call_arguments.done events
    │
    ▼
restore_responses_tool_call(tool_call, item_id, status, tool_map)
    ├── Look up name in tool_map
    ├── If found with "original_tool": → restore_tool_call_from_original()
    │   ├── type "function" → function_call
    │   ├── type "custom" → custom_tool_call
    │   ├── type "namespace" → function_call with namespace field
    │   ├── type "tool_search" → tool_search_call
    │   └── type "web_search" → web_search_call
    └── If not found: → generic function_call (fallback)
```

---

## 11. Existing Code Audit

### Keep (core, stable)

| File | Function | Lines | Why keep |
|------|----------|-------|----------|
| `server-chat.cpp` | `build_responses_tool_map()` | 351-416 | Core mapping builder, handles all 5 types |
| `server-chat.cpp` | `sanitize_tool_name()` | 29-48 | Used everywhere, stable |
| `server-chat.cpp` | `parse_xml_tool_call_fallback()` | ~500-760 | Current fallback parser, needs extension |
| `server-chat.cpp` | `parse_invoke_block()` | ~505+ | Helper for `<invoke>` format |
| `server-task.cpp` | `restore_tool_call_from_original()` | 711-815 | Output restorer, handles all types |
| `server-task.cpp` | `restore_responses_tool_call()` | 817-930 | Entry point for restoration |
| `server-task.h` | `task_result_state` | 96+ | State management for parsing |
| `server-common.cpp` | `parse_tool_calls` enable | 1089 | Enables flag when map present |

### Merge/Consolidate

| File | Function | Why merge |
|------|----------|-----------|
| `server-chat.cpp` | `responses_tool_to_chatcmpl_tools()` | 221-350 | Converted to chatcmpl format; no longer used for injection. Keep as reference but remove call sites. |
| `server-chat.cpp` | `find_web_search_replacement()` | 414-503 | Web search bridge replacement; no longer needed since we don't inject tools. Keep as inline if needed for backward compat. |

### Delete (no longer used since tools not injected)

- `server-chat.cpp`: `chatcmpl_body["tools"] = chatcmpl_tools` block (already removed)
- `server-chat.cpp`: web_search native/replacement loop (already removed)
- `server-chat.cpp`: `responses_tool_to_chatcmpl_tools()` call sites (already removed)

### Future gaps to fill

| Gap | Priority | Approach |
|-----|----------|----------|
| tool_search output round-trip | Medium | Add `tool_search_call` → `tool_search_result` restore in pipeline |
| tool_search input from history | Medium | Handle `tool_search_call` items in history_conversion |
| custom tool `input` vs `arguments` bridging | Low | Already handled in `restore_tool_call_from_original`; verify round-trip |
| Incomplete `<tool_search>` in streaming | Low | Fallback parser already handles partial format D |

---

## 12. Implementation Steps (proposed)

### Phase 1: Consolidate parser (current focus)
1. Refactor `parse_xml_tool_call_fallback` to use `NormalizedToolCall` internally
2. Extract name matching into a deterministic `map_lookup(name, tool_map)` function
3. Add `name_accepted()` per call, not per block
4. Support `<tool_search>query{json}</tool_search>` format
5. Keep `parse_invoke_block` as helper, rename to indicate it's one of many format handlers

### Phase 2: Map lookup
6. Implement `map_lookup()` with the 4-step order from §9
7. Return `AcceptedToolCall` struct with map entry reference
8. Log ambiguous or rejected calls at `[RESP_CTX][WARN]` level

### Phase 3: Cleanup
9. Remove unused `find_web_search_replacement` and `responses_tool_to_chatcmpl_tools` entirely
10. Verify nothing breaks in `server-context.cpp` or `server-task.cpp`

---

## 13. Test Matrix

| Test case | Expected result | Phase |
|-----------|----------------|-------|
| `<tool_call>get_weather{}</tool_call>` with fn tool_map | 1 tool call, content empty | 1 |
| `<invoke name="get_weather"><parameter name="loc">NYC</parameter></invoke>` | 1 tool call, args `{"loc":"NYC"}` | 1 |
| `<tool_search>{"query":"search terms","limit":5}</tool_search>` | 1 tool_search_call | 1 |
| `{"name":"get_weather","arguments":{"loc":"NYC"}}` | 1 tool call | 1 |
| Text with no `<tool_call>` or `<invoke>` | 0 tool calls | 1 |
| `<tool_call>unknown{}</tool_call>` with empty map | rejected, kept as content | 2 |
| `<tool_call>multi_agent_v1__spawn_agent{}</tool_call>` with map | 1 namespace tool call | 2 |
| `<invoke name="_create_branch">` with both github+supabase maps | ambiguous → rejected | 2 |
| `<tool_call>_create_branch{}</tool_call>` — same | rejected | 2 |
| `<invoke name="create_goal">` with map | 1 tool call | 2 |
| Partial `<tool_call>fn{` during streaming | 1 incomplete tool call | 2 |
| Partial `<invoke name="fn"><parameter name="x">1` | 1 incomplete tool call | 2 |
| `<tool_call>fn1{}fn2{}</tool_call>` — fn1 accepted, fn2 rejected | 1 accepted, fn2's text retained as content | 2 |
| `<tool_call>fn{}</tool_call>` with nullptr map | 1 tool call (backward compat) | 1 |
| Parallel `<tool_call>fn1{}fn2{}</tool_call>` in one block | 2 tool calls | 1 |

---

## 14. References

- Captured JSON: `docs/Codex_Tools_docs/CODEX_ACTUAL_TOOLS.json`
- Compact profile: `docs/Codex_Tools_docs/codex-tools/658ff3c4.json`
- Resolved profile: `docs/Codex_Tools_docs/codex-tools/be3d48ac.json`
- Parser: `tools/server/server-chat.cpp` lines ~29-48 (sanitize), ~500-760 (fallback), ~351-416 (build map)
- Restorer: `tools/server/server-task.cpp` lines ~711-815 (restore_tool_call_from_original), ~817-930 (restore_responses_tool_call)
