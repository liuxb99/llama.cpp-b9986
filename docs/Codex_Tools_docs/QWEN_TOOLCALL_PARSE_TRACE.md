# Qwen3-Coder-Next Tool Call Parse Trace

> **Date**: 2026-07-14
> **Commit**: `194979194` + debug logging additions

---

## 1. Problem Statement

模型已通过 `direct` 策略看到 12 个工具：
```
[RESP_CTX] stage=prompt_ready tool_schema=12 tool_schema_chars=<large> grammar=0
```

但 `response_final` 输出 `fn=0`、`cust=0`，说明 `common_chat_parse()` 没有从模型输出中解析出任何 `tool_calls`。

## 2. Hypothesized Root Causes

### A. 模型输出的是纯文本，不是 tool call

模型可能只是描述了要做什么，但没有以任何可解析格式输出工具调用。

### B. 模型输出的是 `<read_file>` XML，但不在 `<tool_call>` 包装内

XML fallback 解析**只**检查三种包装：
- `<tool_call>...</tool_call>`
- `<invoke name=X>...</invoke>`
- `<tool_search>...</tool_search>`

裸 `<read_file>...</read_file>` **不会被捕获**。

### C. PEG parser 格式不对

`chat_parser_params.format` 如果是 `COMMON_CHAT_FORMAT_CONTENT_ONLY`，PEG parser 只提取文本，不解析工具调用。

### D. tool_calls 被解析出来但后续被清除

可能在 `update_chat_msg()` 的 diff 过滤逻辑中被丢弃。

## 3. Debug Logging Added

在 `tools/server/server-task.cpp` 的 `update_chat_msg()` 函数中添加了以下日志：

### Parser 入口（第 167-174 行）
```
== PARSER_INPUT: generated_text_chars=<N> is_partial=<0/1> format=<format_name> parse_tool_calls=<0/1>
== RAW_OUTPUT_BEGIN (first 500 chars of <N>) ==
<模型原始文本前 500 字符>
== RAW_OUTPUT_END ==
```

### XML fallback（第 182-193 行）
```
== PEG_PARSER: 0 tool_calls, trying XML fallback
== XML_TAG_CHECK: has_xml_tag=<0/1>
== XML_FALLBACK: tool_calls=<N>
```

### Parser 结果（第 195-203 行）
```
== PARSER_RESULT: tool_calls=<N> content_chars=<N>
== TOOL_CALL[<i>]: name=<name> id=<id> arguments_chars=<N>
```

### Final 响应（第 1000-1009 行、第 1092-1097 行）
```
== FINAL_CHAT_MSG: content_chars=<N> tool_calls=<N>
== FINAL_TOOL_CALL[<i>]: name=<name> id=<id> args_chars=<N>
```

## 4. How to Collect Evidence

启动 `llama-server` 后：

```bash
export LLAMA_RESPONSES_CONTEXT_DEBUG=1
export LLAMA_LOG_LEVEL=4  # 或更高以显示 LOG_INF
```

发送 Responses 请求后，在日志中查找：
- `== RAW_OUTPUT_BEGIN` → 完整原始输出
- `== PARSER_RESULT: tool_calls=` → PEG parser 是否找到工具
- `== FINAL_CHAT_MSG: tool_calls=` → 最终消息的工具数量

## 5. If tool_calls=0 After All Parsers

重新编译后，观察以下日志组合：

### Case 1: `== RAW_OUTPUT_BEGIN` 包含 `<read_file>` 但无 `<tool_call>`

**诊断**：模型输出裸 XML。PEG parser 不会解析，XML fallback 也不会捕获。

**对策**：在 `update_chat_msg()` 中添加裸 XML → `<tool_call>` 包装（或使用 Chat Completions 端验证相同的模型/工具是否产生 PEG 可解析格式）。

### Case 2: `== RAW_OUTPUT_BEGIN` 包含 `<tool_call><read_file>...</read_file></tool_call>`

**诊断**：XML fallback 应该捕获。检查 `resp_tool_map` 是否能解析。

**对策**：验证 `parse_xml_tool_call_fallback()` 的 `tool_map` 查找。

### Case 3: `== RAW_OUTPUT_BEGIN` 为空或仅包含停止 token

**诊断**：模型生成了空白或拒绝回答。

**对策**：检查 prompt/grammar 配置。

### Case 4: `== RAW_OUTPUT_BEGIN` 包含类似 `{"name":"read_file","arguments":{...}}` 的 JSON

**诊断**：PEG parser 应该捕获。检查 `format` 和 `parse_tool_calls` 设置。

**对策**：确认 `common_chat_parse()` 使用了正确的 parser 和 format。

## 6. ChatCompletions vs Responses Prompt Comparison

如用户所述，可以运行相同的模型和相同的 `read_file` 工具，分别通过：
- `POST /v1/chat/completions`（预期 tool_calls>0）
- `POST /v1/responses`（当前 tool_calls=0）

然后比较：
- 最终 Prompt
- Grammar
- 模型原始输出
- Parser 输出
- tool_calls 数量

如果 ChatCompletions 成功但 Responses 失败，差异可能在 prompt 构造或 parser params 中。

## 7. Expected Log Output (After You Compile)

```text
srv  update_chat_msg: == PARSER_INPUT: generated_text_chars=... is_partial=... format=... parse_tool_calls=...
srv  update_chat_msg: == RAW_OUTPUT_BEGIN (first 500 chars of ...) ==
<模型输出>
== RAW_OUTPUT_END ==
srv  update_chat_msg: == PARSER_RESULT: tool_calls=... content_chars=...
srv  to_json_oaicompat_resp: == FINAL_CHAT_MSG: content_chars=... tool_calls=...
```

## 8. Root Cause (To Be Confirmed by User)

**最可能根因**：模型输出裸 `<read_file>` XML（无 `<tool_call>` 包装），PEG parser 不识别此格式，XML fallback 也不识别，导致 `tool_calls=0`。

**次可能**：`chat_parser_params.format == CONTENT_ONLY`，导致 PEG parser 被跳过且 XML fallback 也未命中。

**不确定点**：需要 `== RAW_OUTPUT_BEGIN` 日志确认模型实际输出什么。
