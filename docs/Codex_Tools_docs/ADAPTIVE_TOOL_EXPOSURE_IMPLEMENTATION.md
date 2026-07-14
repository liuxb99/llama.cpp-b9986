# Adaptive Tool Exposure — Implementation Report

> **Commit**: `601188e2b` 之后最新
> **Date**: 2026-07-14

---

## 1. Root Cause

**无条件 `chatcmpl_body.erase("tools")`** 在 `server-chat.cpp` 的 `oai_to_chatcmpl()` 函数中。

**位置**：`tools/server/server-chat.cpp` 原来第 1549 行（修改前）。

**历史**：来自 commit `400f57354` (fix(responses): keep tool definitions outside model prompt)。该 commit 引入无条件 erase 以防止工具 schema 注入 prompt。虽然 `build_responses_tool_map()` 和 `__responses_tool_map` 被保留用于输出恢复，但模型完全看不到任何工具定义。

**影响**：
- `tool_schema=0`（模型看不到工具 schema）
- `grammar=0`（无工具调用语法约束）
- 模型无法生成原生 `tool_calls`
- 代码通过 XML fallback parser 尝试解析 `<tool_call>`，但 Codex CLI 不依赖此格式
- 结果是模型直接输出 `<read_file><path>...</path></read_file>` 纯文本

## 2. Old Logic

```
oai_to_chatcmpl()
  ├── build_responses_tool_map() → __responses_tool_map  ✅
  ├── chatcmpl_body.erase("tools")  ❌ unconditionally
  ├── if (!chatcmpl_body.contains("tools")) erase("tool_choice")
  └── return chatcmpl_body (no tools → tool_schema=0)
```

## 3. New Strategy: `ToolExposure`

新增 `tools/server/server-chat.cpp` 中的纯函数 `decide_responses_tool_exposure()`：

```cpp
enum class ToolExposure {
    direct,       // Inject tools into chat completion body
    search_only,  // Erase tools, keep tool_map for output restoration
    none,         // No tools at all
};
```

### Decision Logic

```
decide_responses_tool_exposure(counts, schema_chars):
  1. expanded == 0 → none
  2. expanded <= 32 AND schema_chars <= 65536 AND ns_sub <= 32 → direct
  3. otherwise → search_only
```

### Thresholds

| Threshold | Default | Purpose |
|-----------|---------|---------|
| `DIRECT_MAX_TOOLS` | 32 | Maximum tools (after namespace expansion) for direct injection |
| `DIRECT_MAX_SCHEMA_CHARS` | 65536 | Maximum tool schema JSON chars for direct injection |
| `DIRECT_MAX_NAMESPACE_SUB` | 32 | Maximum namespace sub-tools for direct injection |

### Data Flow Per Strategy

#### `direct`（小工具集）

```
oai_to_chatcmpl()
  ├── count_responses_tools() → counts: expanded=12
  ├── schema_chars = 67932 (estimate from request body)
  ├── decide = direct (12 <= 32 AND schema_chars ~OK AND ns_sub=1 <= 32)
  ├── responses_tool_to_chatcmpl_tools() → chatcmpl_body["tools"]  ✅
  ├── build_responses_tool_map() → __responses_tool_map  ✅
  ├── tool_choice preserved
  ├── return chatcmpl_body (has tools → tool_schema>0)
  └── model sees tool schema → generates native tool_calls
```

#### `search_only`（大工具集）

```
oai_to_chatcmpl()
  ├── count_responses_tools() → counts: expanded=259
  ├── schema_chars ≈ 335817
  ├── decide = search_only (259 > 32 OR schema > 65536)
  ├── chatcmpl_body.erase("tools")  ✅ (prevent 335K chars in prompt)
  ├── build_responses_tool_map() → __responses_tool_map  ✅ (for output restore)
  ├── tool_choice erased
  ├── return chatcmpl_body (no tools → tool_schema=0)
  └── model sees no tools → needs tool_search P1 for discovery
```

#### `none`（无工具）

```
oai_to_chatcmpl()
  ├── no tools in request
  ├── decide = none
  ├── no tool_map, no tools
  └── return chatcmpl_body (no tools)
```

## 4. 12-Tool Codex Request: Expected Diagnostics

### `request_convert_start`
```
[RESP_CTX] stage=request_convert_start tools=12 fn=9 cust=1 ns=1 ns_sub=1 ts=1 ws=0
```

### `request_convert_end`
```
[RESP_CTX] stage=request_convert_end tools=12 tool_map=12 has_tools=1 has_map=1
            exposure=direct direct=12 deferred=0 schema_before=67932 schema_after=<after> reason=small_tool_set
```

### `prompt_ready`
```
[RESP_CTX] stage=prompt_ready tool_schema=12 tool_schema_chars=<large> map_marker=1 grammar=0
```

`grammar=0` 是因为当前聊天模板未启用语法约束；`tool_schema>0` 是因为 tools 已注入模型。

## 5. 259-Tool Codex Request: Expected Diagnostics

### `request_convert_start`
```
[RESP_CTX] stage=request_convert_start tools=30 fn=9 cust=1 ns=17 ns_sub=246 ts=1 ws=0
```

### `request_convert_end`
```
[RESP_CTX] stage=request_convert_end tools=0 tool_map=259 has_tools=0 has_map=1
            exposure=search_only direct=0 deferred=259 schema_before=335817 schema_after=0 reason=large_tool_set
```

### `prompt_ready`
```
[RESP_CTX] stage=prompt_ready tool_schema=0 tool_schema_chars=0 map_marker=1 grammar=0
```

## 6. Namespace Handling (direct mode)

`responses_tool_to_chatcmpl_tools()` 函数负责 namespace 展开：

- 每个 `namespace` 工具的子工具被转换为独立的 Chat Completions function tool
- 内部名称：`<namespace_sanitized>__<sub_tool_sanitized>`（例如 `mcp__codex_apps__github__get_issue`）
- 使用现有 `sanitize_tool_name()` 确保无冲突
- `build_responses_tool_map()` 已记录 `original_type: "namespace"`、`original_name`、`namespace_name`
- 输出恢复时，`restore_tool_call_from_original()` 使用原始类型和名称生成正确的 `function_call` + `namespace` 字段

## 7. Custom Tool Handling (direct mode)

`responses_tool_to_chatcmpl_tools()` 处理 custom 工具：

- 内部模型侧：转换为带 `input` 字符串参数的代理 function tool
- `build_responses_tool_map()` 记录 `original_type: "custom"`
- 输出恢复时，`restore_tool_call_from_original()` 使用 `input` 字段（不是 `arguments`）
- 输出类型：`custom_tool_call`（不是 `function_call`）
- Custom 类型信息通过 `build_responses_tool_map()` 的 `original_type` 和 `original_tool` 字段完整保留

## 8. Risks

| Risk | Mitigation |
|------|------------|
| 大工具集注入导致 prompt 超长 | `search_only` 策略限制 schema_chars < 65536 |
| 命名空间子工具名称冲突 | `sanitize_tool_name()` + 复合 key（`ns__name`）保证唯一 |
| Custom 工具输出时误用 `arguments` | `restore_tool_call_from_original()` 显式区分 `input`/`arguments` |
| tool_choice 在无工具时残留 | 在无工具时自动 `erase("tool_choice")` |
| 现有 Responses output bridge 破坏 | `restore_responses_tool_call()`、`output_item.done`、`completed` 均未改动 |
| XML fallback 在 direct 模式下不必要 | remains available as fallback for models that don't use PEG grammar |

## 9. E2E Verification Steps

1. Build: `cmake --build . --target llama-server --config Release`
2. Start: `llama-server -m <model> --port 8080`
3. Configure Codex CLI: `~/.codex/config.toml` with `wire_api = "responses"`
4. Test: `codex "请读取附件 pasted-text.txt，并总结其中的 Go 项目要求。"`
5. Check: model generates native tool_calls (not `<read_file>` text)
6. Check: `common_chat_message.tool_calls > 0`
7. Check: server sends `response.output_item.done(function_call)`
8. Check: Codex executes `read_file`
9. Check: `function_call_output` returned
10. Check: model continues with answer

## 10. Files Changed

| File | Change |
|------|--------|
| `tools/server/server-chat.cpp` | Added `ToolExposure`, `ToolExposureCounts`, `count_responses_tools()`, `decide_responses_tool_exposure()`, `tool_exposure_to_str()`. Modified `oai_to_chatcmpl()` to use exposure strategy. Updated diagnostics. |
