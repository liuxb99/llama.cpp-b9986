# Qwen Known-Tool XML Parser

> **Date**: 2026-07-14
> **Commit**: Latest master

---

## Root Cause

Qwen3-Coder-Next 模型的 chat template 定义工具调用格式为：

```xml
<tool_call>
<function=name>
<parameter=key>value</parameter>
</function>
</tool_call>
```

但实际模型输出的是**裸 XML 标签**，没有 `<tool_call>` 包装：

```xml
<read_file>
<path>C:\...\pasted-text.txt</path>
</read_file>
```

现有 parser 栈全部未命中：
1. **PEG parser**：期望 `[TOOL_CALLS]name[ARGS]{json}` — 不匹配
2. **`<tool_call>` 内部解析**：只支持 `name{json}` 和 `{"name":...}` — 不匹配 `function=name` 格式
3. **XML fallback**：只检查 `<tool_call>`/`<invoke>`/`<tool_search>` 包装 — 裸 `<read_file>` 不匹配

## New Parser: `parse_known_tool_xml_calls()`

**文件**：`tools/server/server-chat.cpp`（新增）

**声明**：`tools/server/server-chat.h`

### 函数签名

```cpp
void parse_known_tool_xml_calls(
    const std::string & text,
    bool is_partial,
    const std::map<std::string, nlohmann::ordered_json> & tool_map,
    std::vector<NormalizedToolCall> & out_calls,
    std::string & clean_content);
```

### 内部辅助函数

```cpp
static bool scan_known_tool_xml_block(
    const std::string & text,
    size_t & pos,
    const std::map<std::string, nlohmann::ordered_json> & tool_map,
    std::string & out_name,
    std::string & out_args_json,
    std::string & out_namespace,
    std::string & out_source,
    bool is_partial,
    bool & out_partial);
```

## 解析规则

### 格式识别

扫描文本中的 `<tagname>...</tagname>` 块：
1. 提取根标签名 `tagname`
2. 查找对应的 `</tagname>` 结束标签
3. 解析内部子标签为参数名/值对

### 工具名解析（安全边界）

根标签必须匹配 `tool_map` 中的条目，按以下顺序：

| 优先级 | 查找方式 | 示例 |
|--------|----------|------|
| 1 | 精确匹配 map key | `read_file` → map 中有 `read_file` |
| 2 | 经 `sanitize_tool_name()` 匹配 | `read-file` → `read_file` |
| 3 | `custom_tool_` 前缀匹配 | `apply_patch` → `custom_tool_apply_patch` |
| 4 | `resolve_tool_name()` 最终解析 | 通过唯一 original_name 匹配 |

**拒绝条件**：
- 标签名不匹配任何 map key → 保持为普通文本
- 多个 namespace 有同名子工具（歧义）→ 拒绝，记录 `ambiguous`
- 原始名称冲突 → 不猜测

### 参数解析（安全边界）

| 检查 | 处理方式 |
|------|----------|
| 未知参数名 | 拒绝，标记 `reject_reason=unknown_param` |
| 重复参数 | 拒绝，标记 `reject_reason=duplicate_param` |
| required 参数缺失 | 不标记 completed（partial 流可接受） |
| DOCTYPE/ENTITY | `<!` `<?` 开头直接跳过 |
| XML 属性 | 不作为参数注入 |
| CDATA | 不执行 |
| 参数值转义 | 纯文本提取 |

### 参数值类型转换

| schema 类型 | 转换方式 |
|------------|----------|
| (无 schema) | 保留为字符串 |
| `string` | 保留为字符串 |
| `boolean` | `"true"`/`"false"` → JSON boolean |
| `integer` | 纯数字 → JSON integer |
| `number` | 含小数点 → JSON number |
| `array`/`object` | 不转换（保持字符串） |
| 无法合法转换 | 拒绝，标记 `reject_reason=type_error` |

## function 示例

### 输入
```xml
<read_file>
<path>C:\Users\me\file.txt</path>
</read_file>
```

### 输出
```cpp
NormalizedToolCall {
    name = "read_file",
    arguments = "{\"path\":\"C:\\\\Users\\\\me\\\\file.txt\"}",
    source_format = "known_tool_xml",
    partial = false
}
```

### 最终 Response item
```json
{"type":"function_call","id":"fc_...","call_id":"call_...","name":"read_file","arguments":"{\"path\":\"C:\\\\Users\\\\me\\\\file.txt\"}"}
```

## custom 示例

### 输入
```xml
<apply_patch>
<input>*** Begin Patch
--- a/file.txt
+++ b/file.txt
@@ -1 +1 @@
-old
+new
*** End Patch
</input>
</apply_patch>
```

### 输出
```cpp
NormalizedToolCall {
    name = "apply_patch",
    arguments = "{\"input\":\"*** Begin Patch\\n--- a/file.txt...\"}",
    source_format = "known_tool_xml",
    partial = false
}
```

### 最终 Response item
```json
{"type":"custom_tool_call","id":"ctc_...","call_id":"call_...","name":"apply_patch","input":"*** Begin Patch\\n--- a/file.txt..."}
```

## namespace 示例

### 输入（模型使用内部唯一名）
```xml
<mcp__github__get_issue>
<number>1</number>
</mcp__github__get_issue>
```

### 解析
- `map_key = "mcp__github__get_issue"`（精确匹配）
- `original_type = "namespace"`
- `namespace_name = "github"`
- `original_name = "get_issue"`

### 最终 Response item
```json
{"type":"function_call","id":"fc_...","call_id":"call_...","namespace":"github","name":"get_issue","arguments":"{\"number\":1}"}
```

### 歧义处理
若多个 namespace 有相同 `original_name`（如 `ns1.get_issue` 和 `ns2.get_issue`），模型输出短名 `<get_issue>` 时，`resolve_tool_name("get_issue", "", tool_map)` 会因为重名冲突返回空字符串，解析被拒绝。

## 解析优先顺序

```
1. PEG/structured tool_calls  ← llama.cpp 原生
2. <tool_call>name{json}</tool_call>
3. <invoke name="X">...</invoke>
4. <tool_search>...</tool_search>
5. 已知工具 XML（新）            ← parse_known_tool_xml_calls()
6. 普通 assistant text
```

## 接入位置

在 `update_chat_msg()`（`server-task.cpp`）中，插入在 `<tool_call>`/`<invoke>`/`<tool_search>` XML fallback 之后、进入普通文本处理之前：

```cpp
// Try XML fallback when PEG parser found no tool calls
if (new_msg.tool_calls.empty()) {
    // ... existing <tool_call>/<invoke>/<tool_search> check ...

    // Known-tool XML fallback: try bare <tool_name><param>value</param></tool_name>
    if (new_msg.tool_calls.empty() && resp_tool_map && !resp_tool_map->empty()) {
        std::vector<NormalizedToolCall> known_calls;
        std::string known_clean;
        parse_known_tool_xml_calls(generated_text, is_partial, *resp_tool_map, known_calls, known_clean);
        if (!known_calls.empty()) {
            normalized_calls_to_chat_msg(new_msg, known_calls);
        }
    }
}
```

## 调试日志

启用 `LLAMA_RESPONSES_CONTEXT_DEBUG=1` 后可看到：

```
[RESP_TOOL_PARSE] source=known_tool_xml root=read_file resolved=read_file type=function ns= args=1 ambig=0 reject=
[RESP_TOOL_PARSE] source=known_tool_xml root=apply_patch resolved=apply_patch type=custom ns= args=1 ambig=0 reject=
[RESP_TOOL_PARSE] source=known_tool_xml root=mcp__github__get_issue resolved=mcp__github__get_issue type=function ns=github args=1 ambig=0 reject=
[RESP_TOOL_PARSE] source=known_tool_xml root=get_issue resolved= type=function ns= args=0 ambig=1 reject=duplicate_original_name
[RESP_TOOL_PARSE] source=known_tool_xml root=unknown_tag resolved= type= ns= args=0 ambig=0 reject=unknown_root_tag
```

## 测试

| # | 测试 | 文件 |
|---|------|------|
| 1 | 单个 read_file XML | `test_known_tool_xml.py` |
| 2 | shell_command XML | 同上 |
| 3 | custom tool | 同上 |
| 4 | namespace 展开 | 同上 |
| 5 | 无工具时保持文本 | 同上 |
| 6 | 多工具请求 | 同上 |
| 7 | 流式事件 | 同上 |
| 8 | 现有 `<tool_call>` 回归 | 同上 |
| 9 | 输出格式验证 | 同上 |

## 安全边界总结

| 风险 | 缓解措施 |
|------|----------|
| 未知标签冒充工具 | 拒绝，保持为文本 |
| 外部实体/DOCTYPE | `<!` 跳过 |
| 参数注入 | 只识别人工提取的子标签，不支持属性 |
| 类型混淆 | 按 schema 类型安全转换，不可转换则拒绝 |
| namespace 同名冲突 | `resolve_tool_name()` 拒绝歧义 |
| custom 类型丢失 | `original_type` 保存在 tool_map，restore 时恢复 |

## E2E 验证

```bash
# 编译
cmake --build . --target llama-server --config Release

# 启动（启用调试日志）
export LLAMA_RESPONSES_CONTEXT_DEBUG=1
export LLAMA_LOG_LEVEL=4
llama-server -m Qwen3-Coder-Next -p 8080

# 运行 Codex CLI
codex "请读取附件 pasted-text.txt，并总结其中的 Go 项目要求。"

# 预期日志
# [RESP_TOOL_PARSE] source=known_tool_xml root=read_file resolved=read_file ...
# == KNOWN_TOOL_XML: tool_calls=1
# == PARSER_RESULT: tool_calls=1
# response_final ... fn=1

# 预期行为
# - 模型不再输出普通 <read_file> 文字
# - common_chat_message.tool_calls > 0
# - server 发 response.output_item.done(function_call)
# - Codex CLI 真正执行 read_file
