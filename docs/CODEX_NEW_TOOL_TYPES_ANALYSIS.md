# Codex CLI Modern Tool Types Analysis

> **Date:** 2026-07-13
> **Source:** openai/codex (main branch, codex-rs/)
> **Files analyzed:** `tool_spec.rs`, `responses_api.rs`, `dynamic_tools.rs`, `tool_config.rs`

---

## 1. Overview

Codex CLI 5 tool types are defined in `ToolSpec` enum (`tool_spec.rs`):

| # | Enum variant | JSON `"type"` | C++ support |
|---|-------------|---------------|-------------|
| 1 | `Function(ResponsesApiTool)` | `"function"` | ✅ Existing |
| 2 | `Namespace(ResponsesApiNamespace)` | `"namespace"` | ✅ Backport (125825b8b) |
| 3 | `Freeform(FreeformTool)` | `"custom"` | ✅ Backport (125825b8b) |
| 4 | `ToolSearch{...}` | `"tool_search"` | ✅ Backport (125825b8b) |
| 5 | `WebSearch{...}` | `"web_search"` | ⚠️ Skip + warning (no local backend) |

---

## 2. Function Tool

```rust
pub struct ResponsesApiTool {
    pub name: String,
    pub description: String,
    pub strict: bool,           // false for Codex custom
    pub defer_loading: Option<bool>,
    pub parameters: JsonSchema,
}
```

**JSON:**
```json
{
  "type": "function",
  "name": "get_weather",
  "description": "Get weather for a city",
  "strict": true,
  "parameters": { "type": "object", "properties": { "city": { "type": "string" } } }
}
```

**Conversion:** Direct passthrough with `sanitize_tool_name()`. ✅

---

## 3. Namespace Tool

```rust
pub struct ResponsesApiNamespace {
    pub name: String,
    pub description: String,
    pub tools: Vec<ResponsesApiNamespaceTool>,
}

pub enum ResponsesApiNamespaceTool {
    Function(ResponsesApiTool),   // only variant
}
```

**JSON:**
```json
{
  "type": "namespace",
  "name": "mcp__demo__",
  "description": "Demo MCP tools",
  "tools": [
    {
      "type": "function",
      "name": "lookup_order",
      "description": "Look up an order",
      "strict": false,
      "parameters": { "type": "object", "properties": { "id": { "type": "string" } } }
    }
  ]
}
```

**Conversion:** Expand each `tools[].function` into a standalone function tool with name `"{namespace}__{tool_name}"`. The `__` delimiter allows Codex to reverse-map the call back to the correct namespace. ✅

**Multiple namespaces:** Each namespace expands independently. Same sub-tool names in different namespaces get unique qualified names (`ns1__tool_a`, `ns2__tool_a`). ✅

**Name sanitization:** Both namespace name and sub-tool name go through `sanitize_tool_name()`. ✅

---

## 4. Custom (Freeform) Tool

### Struct (from `tool_spec.rs`):

```rust
pub struct FreeformTool {
    pub name: String,
    pub description: String,
    pub format: FreeformToolFormat,
}

pub struct FreeformToolFormat {
    pub r#type: String,     // e.g. "grammar"
    pub syntax: String,     // e.g. "lark"
    pub definition: String, // e.g. grammar definition
}
```

### JSON:
```json
{
  "type": "custom",
  "name": "exec",
  "description": "Execute a shell command",
  "format": {
    "type": "grammar",
    "syntax": "lark",
    "definition": "start: command\\ncommand: ..."
  }
}
```

### Codex Tools That Use `custom`:

| Codex tool | `name` | Description | Format |
|------------|--------|-------------|--------|
| Shell exec | `"exec"` | Run a shell command | grammar |
| Read file | `"read"` | Read file contents | grammar |
| Edit/Apply patch | `"apply_patch"` or `"edit"` | Edit a file | grammar |
| File search | `"grep"` or `"search"` | Search in files | grammar |

All current Codex built-in tools (`exec`, `read`, `edit`, `apply_patch`, `grep`) are **free-form custom** tools. They have no JSON schema; their format is defined by a grammar.

### Conversion Strategy:

Wrap free-form input in a `{"input": "..."}` JSON object so the standard function-calling infrastructure handles it:

```json
{
  "type": "function",
  "function": {
    "name": "exec",
    "description": "Execute a shell command",
    "parameters": {
      "type": "object",
      "properties": {
        "input": { "type": "string", "description": "Free-form input for the custom tool" }
      },
      "required": ["input"],
      "additionalProperties": false
    },
    "strict": false
  }
}
```

The model generates `{"input": "ls -la"}` which is a valid JSON object, so `tool_call_arguments_valid()` passes. ✅

### JSON-schema custom tool:

If a `custom` tool ever gains a `parameters` field (future Codex versions), it falls through as a JSON-schema function tool naturally. ✅

---

## 5. Tool Search Tool

### Inline struct:
```rust
ToolSearch {
    execution: String,       // always "sync"
    description: String,
    parameters: JsonSchema,  // typically {query: string}
}
```

### JSON:
```json
{
  "type": "tool_search",
  "execution": "sync",
  "description": "Search for tools matching the user request",
  "parameters": {
    "type": "object",
    "properties": {
      "query": { "type": "string", "description": "Tool search query" }
    },
    "required": ["query"],
    "additionalProperties": false
  }
}
```

### Conversion:

Convert to a function tool named `"tool_search"` with the query parameter schema. The model generates `tool_search` function calls; these are relayed back to Codex client-side for execution. ✅

**No `--responses-tool-search` config needed** — passthrough is the default and only behavior.

---

## 6. Web Search Tool

### Inline struct:
```rust
WebSearch {
    external_web_access: Option<bool>,
    indexed_web_access: Option<bool>,
    filters: Option<ResponsesApiWebSearchFilters>,
    user_location: Option<ResponsesApiWebSearchUserLocation>,
    search_context_size: Option<WebSearchContextSize>,
    search_content_types: Option<Vec<String>>,
}
```

### JSON:
```json
{
  "type": "web_search",
  "external_web_access": true,
  "indexed_web_access": true
}
```

### Conversion:

llama.cpp has no web search backend. Web search tools are **skipped with a warning** but do not block other tools. ✅

---

## 7. Output Type Mapping

| Input tool type | Model generates | Responses API output type | Codex accepts |
|----------------|----------------|--------------------------|---------------|
| `function` | `function_call` | `{"type": "function_call"}` | ✅ |
| `namespace` | `function_call` (qualified name) | `{"type": "function_call"}` | ✅ (reverse-maps via `__`) |
| `custom` | `function_call` with `{"input": ...}` | `{"type": "function_call"}` | ✅ |
| `tool_search` | `function_call` (name=`tool_search`) | `{"type": "function_call"}` | ✅ |
| `web_search` | N/A (skipped) | N/A | N/A |

All converted tools produce `function_call` output. Codex CLI accepts `function_call` responses for all tool types because it registered them as function tools during the conversion. ✅

---

## 8. Input History Round-trip

When Codex sends multi-turn history containing tool call results, the input items use these types (from `server_chat_convert_responses_to_chatcmpl()`):

| History item type | Codex sends | Current support |
|-------------------|-------------|-----------------|
| `function_call` | `{"type": "function_call"}` | ✅ |
| `function_call_output` | `{"type": "function_call_output"}` | ✅ |
| `custom_tool_call` | `{"type": "custom_tool_call"}` | ✅ |
| `custom_tool_call_output` | `{"type": "custom_tool_call_output"}` | ✅ |
| `mcp_tool_call_output` | `{"type": "mcp_tool_call_output"}` | ✅ |
| `web_search_output` | `{"type": "web_search_output"}` | ✅ |
| `file_search_output` | `{"type": "file_search_output"}` | ✅ |
| `tool_search_output` | `{"type": "tool_search_output"}` | ✅ |

All output types with `call_id` and `output` fields are handled by `append_tool_output_message()`. ✅

---

## 9. Safety Checks

| Check | Status |
|-------|--------|
| malformed JSON → no HTTP 500 | ✅ (eb29686) |
| incomplete function arguments → not completed | ✅ (f9952592b) |
| free-form custom tool → not rejected by JSON object validator | ✅ (wrapped in `{"input": ...}`) |
| EOS → not overridden | ✅ (eb29686) |
| context/client cancel → hard stop | ✅ (eb29686) |

---

## 10. Files Modified

- `tools/server/server-chat.cpp` — `responses_tool_to_chatcmpl_tools()` rewritten to support 5 tool types
- `tools/server/tests/unit/test_compat_oai_responses.py` — 10 new tests covering all new types
