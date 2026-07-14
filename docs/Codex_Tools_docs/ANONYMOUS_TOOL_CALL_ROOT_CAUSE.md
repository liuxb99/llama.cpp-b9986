# Anonymous Tool Call Root Cause Analysis

## Observed Output

```
<tool_call>
```powershell
...

```

(No closing `</tool_call>`, no tool name, content is a fenced PowerShell code block.)

---

## 1. Which Layer Produces This Output

**Model** — the LLM itself generates these tokens.

Pipeline trace:

| Layer | Role | Evidence |
|-------|------|----------|
| Chat template | Formats conversation + injects `tools` array | Templates use `<tool_call>` tokens per `auto-parser*.cpp` analysis, but the template itself does not insert anonymous blocks |
| Model | Generates output tokens | The `<tool_call>\`\`\`powershell...` sequence is emitted token-by-token by the LLM |
| Normalizer (`parse_normalized_tool_calls`) | Parses `<tool_call>` blocks | Correctly rejects this block (falls to `clean_content`) — see §4 |
| Restore (`restore_responses_tool_call`) | Converts internal format → Responses API | Never reached because normalizer produces zero tool calls |
| Codex runtime | Sends `tools[]` + receives inference | Provides the tool definitions but does not generate output |

**Conclusion**: The model intended to call a shell tool but used a corrupted format.

---

## 2. Why There Is No Tool Name

The model is conflating **two learned patterns**:

| Pattern | Origin | Typical Form |
|---------|--------|-------------|
| Tool call via XML tag | Chat template / training data for tool-use models | `<tool_call>function_name{...}</tool_call>` |
| Shell command via fenced code block | Training data (Markdown, technical writing) | `` ```powershell\n...\n``` `` |

The model merges them: it wraps a fenced PowerShell block **inside** `<tool_call>` instead of writing `<tool_call>shell_command{"command":"..."}</tool_call>`. It treats `<tool_call>` as a generic "execute this" wrapper, not as a structured name+JSON envelope.

**Missing token**: After `<tool_call>`, the normalizer expects either `tool_name` (starts with `[a-zA-Z_]`) or `{` (JSON object). The model emits `` ` `` (backtick) — which triggers neither format and falls through to the unrecognized branch.

---

## 3. Does It Match Any Real `tools[]` Schema?

**No.** Every real tool in both captured profiles (658ff3c4 and be3d48ac) requires:

- A **name** (e.g. `shell_command`, `read_file`, `update_plan`)
- **arguments** as a JSON object (e.g. `{"command": "..."}`)

The observed output:
- Has **no tool name**
- Has **no JSON arguments**
- Has a **bare fenced code block** with no structured parameters

The `shell_command` tool (which the model almost certainly intended to call) expects:
```json
{"command": "<powershell command string>"}
```

A fenced code block inside `<tool_call>` with no name and no JSON does not match any tool definition.

---

## 4. Why the Unified Normalizer Cannot Parse This

Normalizer entry point: `parse_normalized_tool_calls()` (line 648, `server-chat.cpp`).

The parser works as follows when it encounters `<tool_call>`:

```
1. inner_start = position after "<tool_call>"
2. Scan for </tool_call>             → not found, block_end = text.size()
3. Skip whitespace
4. Check Format A (isalpha or '_'):  text[ci] = '`' → NO
5. Check Format B ( '{' ):           text[ci] = '`' → NO
6. Fall through → break (unrecognized)
7. any_accepted == false → entire block appended to clean_content
```

**The normalizer is behaving correctly.** There is no parser bug. The model produced a call whose syntax falls outside every defined format. The parser's rejection (keeping the text as plain content) is the expected safe fallback — the malformed call is never dispatched, and the text appears as normal assistant content.

**Attempting to "fix" the parser** to recognize anonymous fenced blocks would be fragile:
- Guessing which tool the model intended (shell_command? bash? python?) is ambiguous
- Extracting command text from fenced blocks requires heuristics that can break on multi-line commands, inline scripts, or mixed content
- Every heuristic becomes a maintenance burden and a source of false positives

---

## 5. Correct Minimal Fix Location

**Chat template / generation prompt** — NOT the normalizer (parser).

The root cause is the model not following the correct `<tool_call>tool_name{...}</tool_call>` format. The fix is to **guide the model** with a short output format hint before generation begins.

Current system prompt construction (see `server-chat.cpp` system prompt builder around line 1664 and the `gen_prompt` stripping at line 660) already provides format information, but it lacks a concrete example of what NOT to do.

| Layer | Fix? | Why |
|-------|------|-----|
| Chat template | **Yes** | Add 1-2 sentences to the instruction that appears before the model generates |
| Normalizer | **No** | Correctly rejects malformed input; adding guesses is fragile |
| Restore | **No** | Never reached for rejected calls |
| Codex runtime | **No** | Not responsible for output format |

---

## 6. Recommended Format Hint

Add a short instruction to the injected tool-use prefix / system prompt — 2 lines maximum:

```
Output tool calls as: <tool_call>tool_name{...}</tool_call>
Do NOT nest fenced code blocks inside <tool_call>.
```

Or more precisely, if the `shell_command` tool is present:

```
To execute a shell command, output <tool_call>shell_command{"command":"..."}</tool_call>
Do NOT wrap commands in ``` fences inside <tool_call>.
```

**Why 2 lines is enough**:
- It directly tells the model the correct syntax
- It explicitly forbids the observed failure pattern (fenced block nesting)
- It does not require any parser changes
- It is cheap (negligible token cost) and model-agnostic

**Do NOT add parser heuristics** for anonymous fenced blocks — that would legitimise a format the model should not use, and would introduce ambiguity and maintenance cost.

---

## Summary

| Question | Answer |
|----------|--------|
| Producing layer | **Model** |
| No tool name | Model conflated `<tool_call>` envelope with Markdown fenced code block |
| Matches any tools[] schema | **No** |
| Normalizer can't parse | Parser checks for name or `{` first; finds backtick, correctly rejects |
| Where to fix | **Chat template / generation prompt** — short format hint |
| Parser changes needed | **None** — adding heuristics would be fragile and wrong |

The smallest correct fix is to guide the model output format, not to guess at the parser level.
