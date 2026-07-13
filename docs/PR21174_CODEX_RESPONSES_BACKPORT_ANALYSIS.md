# PR #21174 Backport Analysis: Responses API / Codex CLI Compatibility

> **Date:** 2026-07-13
> **Target repo:** `ggml-org/llama.cpp` (PR #21174)
> **Author branch:** `krystophny:responses-api-codex-compat`
> **Base branch:** `master`
> **Local baseline:** `b9986` + commit `eb29686` (incomplete tool-call fix)

---

## 1. PR Overview

**Title:** server: improve Responses API compliance and Codex CLI compatibility

**State:** OPEN (not merged)

### 1.1 Commits (22 total)

| # | OID | Date | Headline | Type |
|---|-----|------|----------|------|
| 1 | `2004b5d3` | 2026-03-30 | server: improve Responses API compliance and Codex CLI compatibility | substantive |
| 2 | `1dd53bc6` | 2026-03-30 | server: add tests for Responses API compliance and Codex compatibility | tests |
| 3 | `9406735d` | 2026-03-30 | server: add full streaming compliance for Responses API events | substantive |
| 4 | `e008674d` | 2026-03-30 | server: add streaming compliance tests for Responses API | tests |
| 5 | `f88b0a0a` | 2026-03-30 | server: fix streaming event bugs and tighten test assertions | fix |
| 6 | `76e999b5` | 2026-03-30 | ci: retrigger after transient infrastructure failures | ci |
| 7 | `d9dab5e0` | 2026-03-31 | server: fix reasoning item content format handling for multi-turn | substantive |
| 8 | `9ded255e` | 2026-04-06 | server: graceful degradation for unsupported Responses API input types | substantive |
| 9 | `b3b4789d` | 2026-04-06 | server: add visible recovery for malformed Responses history | fix |
| 10 | `f7aabb44` | 2026-05-02 | Add near-complete Codex VSCode Support, full OAI Responses bridge | substantive |
| 11 | `f63f8faa` | 2026-05-03 | server: fix Responses image parser pass | fix |
| 12 | `4c6f3fd1` | 2026-06-06 | Merge remote-tracking branch 'upstream/master' | merge |
| 13 | `61af286a` | 2026-06-16 | server: re-apply no_prefill_assistant gate dropped in upstream merge | fix |
| 14 | `8a5fb8d4` | 2026-06-17 | server: gate Responses image generation exposure | fix |
| 15 | `8e2be75f` | 2026-06-18 | Merge remote-tracking branch 'upstream/master' | merge |
| 16 | `abeb6935` | 2026-06-20 | Merge remote-tracking branch 'upstream/master' | merge |
| 17 | `cc8cf032` | 2026-06-22 | Merge remote-tracking branch 'upstream/master' | merge |
| 18 | `03bf03a4` | 2026-06-23 | server: drop unrelated core-lib changes (now separate PRs) | cleanup |
| 19 | `a4594eb0` | 2026-06-24 | server: remove hosted web/file search shell bridge from Responses API | cleanup |
| 20 | `6f53ceb4` | 2026-06-24 | server: skip non-function Responses tools, relay calls as function_call | cleanup |
| 21 | `e921479a` | 2026-06-24 | server: skip malformed/unknown Responses input parts silently | cleanup |
| 22 | `f91c6782` | 2026-06-24 | server: skip unknown top-level Responses items silently | cleanup |
| 23 | `176abd2b` | 2026-06-28 | Merge branch 'master' (upstream sync) | merge |
| 24 | `8f8009ed` | 2026-06-28 | Merge remote-tracking branch 'upstream/master' | merge |

**Substantive commits (backport candidates):** 1, 3, 7, 8, 10, 13, 14
**Test commits:** 2, 4
**Fix/cleanup commits:** 5, 9, 11, 18, 19, 20, 21, 22
**Merge commits:** 12, 15, 16, 17, 23, 24 (skip - upstream sync only)

### 1.2 Files Changed

| File | +/- | Priority |
|------|-----|----------|
| `tools/server/server-chat.cpp` | +415/-99 | **HIGH** |
| `tools/server/server-task.cpp` | +375/-191 | **HIGH** |
| `tools/server/server-context.cpp` | +83/-2 | **MEDIUM** |
| `tools/server/server-common.cpp` | +23/-4 | **LOW** |
| `tools/server/server-common.h` | +3/-1 | **LOW** |
| `tools/server/server-task.h` | +24/-1 | **MEDIUM** |
| `tools/server/server-schema.cpp` | +0/-1 | **NONE** |
| `tools/server/tests/unit/test_compat_oai_responses.py` | +780/-0 | **LOW** |
| `tools/server/README.md` | +0/-1 | **NONE** |

---

## 2. File-by-File Analysis

### 2.1 `tools/server/server-common.h` (+3/-1)

**Patch adds:**
- Two new boolean parameters to `oaicompat_chat_params_parse()`:
  - `bool no_prefill_assistant = false`
  - `bool unsupported_image_as_text = false`

**Current b9986:** Missing both parameters. Signature has only 3 params.

**Backport needed?** YES — required by server-context.cpp changes.
**Conflict risk:** LOW — additive with default values.
**Minimal change:** Add `= false` defaults to function signature.

### 2.2 `tools/server/server-common.cpp` (+23/-4)

**Patch adds:**
- Implementation of both new params in `oaicompat_chat_params_parse`
- Logic to skip assistant prefill when `no_prefill_assistant` is set and last message is assistant role
- Logic to convert unsupported image content to text placeholder when `unsupported_image_as_text` is set

**Current b9986:** Missing.

**Backport needed?** YES
**Conflict risk:** LOW — additive logic gated by new boolean params.
**Minimal change:** Add parameter passthrough + conditional logic blocks.

### 2.3 `tools/server/server-chat.cpp` (+415/-99) — **KEY FILE**

**Patch changes — Graceful degradation (backport priority: HIGH):**

| Feature | Current b9986 Behavior | Patch Behavior | Status |
|---------|----------------------|----------------|--------|
| `previous_response_id` | Throws error | Logs warning, continues | **BACKPORT** |
| Single object input | Throws | Wraps in array | **BACKPORT** |
| Non-object input items | Crash on `.at()` | Skip silently | **BACKPORT** |
| Missing `text` in `input_text` | Throws | Skip silently | **BACKPORT** |
| `input_file` type | Throws `not supported` | Text placeholder | **BACKPORT** |
| Unknown content type | Throws | Skip silently | **BACKPORT** |
| Unknown top-level item type | Throws | Skip silently | **BACKPORT** |
| Reasoning content null/missing | Throws | Accepts null/string/array | **BACKPORT** |
| `developer` role merging | Not done | Merges into `system` at pos 0 | **BACKPORT** |
| Responses-only key stripping | Not done | Strips `store`, `include`, etc. | **BACKPORT** |
| `tool_choice` object handling | Not handled | Converts to Chat format | **BACKPORT** |
| Tool name sanitization | Basic | Full `sanitize_tool_name()` | **BACKPORT** |
| Function call args parsing | Expects string | Accepts object/array/string | **BACKPORT** |

**Patch changes — Already in b9986 (no backport needed):**
- Skipping non-function tool types with warning ✓
- Adding `strict: true` to function tools ✓
- `max_output_tokens` → `max_tokens` conversion ✓

**Patch adds — New helper functions (backport priority: MEDIUM):**
- `exists_and_is_array()`, `exists_and_is_string()`
- `truncate_for_prompt()`
- `responses_make_text_content()`
- `sanitize_tool_name()`
- `parse_arguments_best_effort()`
- `make_tool_call()`
- `append_assistant_tool_call()`
- `mcp_image_to_data_url()`
- `encode_tool_output_content()`
- `encode_tool_output_content_item()`
- `append_tool_output_message()`
- `input_file_text()`

**Conflict with eb29686:** MEDIUM — tool call assembly logic in `append_assistant_tool_call` merges into last assistant message. Eb29686 changed how tool calls are validated/completed; the merge logic may need adjustment.

**Recommendation:** Backport graceful degradation first (pure additive), then tool call helpers (may need adaptation for eb29686).

### 2.4 `tools/server/server-task.h` (+24/-1)

**Patch adds to `task_result_state`:**
- `std::string oai_resp_fc_item_id` — fc_ item ID
- `std::vector<std::string> oai_resp_fc_item_ids` — all fc_ IDs in order
- `int oai_resp_seq_num = 0` — stream sequencer
- `int oai_resp_output_idx = 0` — current output item index
- `int oai_resp_reasoning_output_idx = -1`
- `bool oai_resp_reasoning_done = false`
- `bool oai_resp_message_done = false`

**Patch adds to `server_task_result_cmpl_final`:**
- Same fields as above (copied from state)

**Patch adds to `server_task_result_cmpl_partial`:**
- Same fields plus:
  - `std::string oai_resp_reasoning_content`
  - `std::string oai_resp_message_content`

**Current b9986:** Only `oai_resp_fc_id`, `thinking_block_started`, `text_block_started` exist.

**Backport needed?** YES — all fields required for streaming compliance.
**Conflict risk:** LOW-MEDIUM — additive fields, but `update()` state machine must be compatible.
**Minimal change:** Add all fields with initializers.

### 2.5 `tools/server/server-task.cpp` (+375/-191) — **KEY FILE**

**Patch changes — Output formatting (backport priority: HIGH):**

**Non-streaming (`to_json_oaicompat_resp`):**
- Replaces inline JSON with helper functions: `build_oai_resp_metadata()`, `build_responses_message_item()`, `build_responses_function_call_item()`, `build_responses_reasoning_item()`
- Adds `output_text` convenience field
- Adds all 24 Response object fields per OpenAI spec (vs. current ~7 fields)
- `usage` gains `output_tokens_details.rea**********ens`
- Handles `incomplete` status for truncation cases
- Function call `id`/`call_id` mapping: `id` gets unique `fc_` ID, `call_id` gets model's tool_call.id

**Streaming (`to_json_oaicompat_resp_stream`):**
- Adds `sequence_number` to ALL SSE events
- Adds `output_index` to output-item events
- Adds `content_index` to content events
- `response.created` / `response.in_progress` carry full response object
- `maybe_close_reasoning()` / `maybe_close_text()` lambdas
- Reasoning summary events: `reasoning_summary_part.added`, `reasoning_summary_text.delta`
- `phase` field on text items (`"commentary"` / `"final_answer"`)
- Consistent `fc_` item IDs across added/delta/done events

**`update()` state machine (risk: HIGH):**
- Advances `oai_resp_seq_num` and `oai_resp_output_idx`
- Closes reasoning/text blocks before tool calls
- Assigns `oai_resp_fc_item_id` (separate from `oai_resp_fc_id`)
- Stores `oai_resp_reasoning_content` and `oai_resp_message_content`
- Counter state persisted across streaming chunks

**Current b9986:** Missing all streaming compliance. Response has only basic fields.

**Backport needed?** YES
**Conflict risk with eb29686:** HIGH for `update()` state machine; LOW-MEDIUM for output formatting (pure data construction).

**Recommendation:** Backport in order: (1) helper functions, (2) non-streaming response changes, (3) streaming output formatting, (4) `update()` state machine changes (requires most testing).

### 2.6 `tools/server/server-context.cpp` (+83/-2)

**Patch changes:**

1. **Two-pass parser** for `post_responses_oai` handler:
   - First pass: with tools → `responses_relax_tool_required_for_parser()`
   - Second pass: without tools → `responses_apply_parser_fields()`
   - Passes `no_prefill_assistant=true` and `unsupported_image_as_text=true`

2. **Error formatting** for `TASK_RESPONSE_TYPE_OAI_RESP`:
   - Structured `response.failed` SSE with `sequence_number`, `id`, `status: "failed"`
   - Typed error codes mapped from server error types

**Current b9986:** Single-pass parser; generic error format.

**Backport needed?** YES for two-pass parser (enables Codex tool calling with proper GBNF grammar constraints). YES for error formatting.
**Conflict risk:** MEDIUM — two-pass parser depends on tool param structure. Eb29686's tool-call validation changes may affect the first pass.
**Minimal change:** Add `responses_relax_tool_required_for_parser()`, `responses_apply_parser_fields()`, modify parser call sites, add error formatting block.

### 2.7 `tools/server/server-schema.cpp` (+0/-1)

**Trivial:** Removes one blank line.

**Backport needed?** NO

### 2.8 `tools/server/tests/unit/test_compat_oai_responses.py` (+780/-0)

**New file** covering all features:
- `test_responses_schema_fields` — 24+ Response object fields
- `test_responses_stream_schema_fields` — sequence_number, output_index, content_index
- `test_responses_non_function_tool_skipped`
- `test_responses_mixed_tool_types`
- `test_responses_extra_keys_stripped`
- `test_responses_developer_role`
- `test_responses_input_text_type`
- `test_responses_function_call_id_fields`
- `test_responses_stream_created_event_has_full_response`
- `test_responses_stream_all_events_have_sequence_number`
- `test_responses_stream_delta_events_have_indices`
- (plus reasoning content format tests from later commits)

**Backport needed?** YES — necessary for regression testing.
**Conflict risk:** LOW — purely additive.

---

## 3. Summary: Already Present vs. Missing

### Already in b9986 (no backport needed)

- Basic Responses → Chat Completions conversion framework ✓
- `format_oai_resp_sse()` function ✓
- Non-function tool type skipping (warning only) ✓
- `strict: true` addition to function tools ✓
- `max_output_tokens` → `max_tokens` ✓
- `oai_resp_id`, `oai_resp_reasoning_id`, `oai_resp_message_id` ✓
- `thinking_block_started` / `text_block_started` ✓

### Missing — Needs Backport

| Priority | Feature | Files | Risk |
|----------|---------|-------|------|
| **P0** | Graceful degradation for input types | server-chat.cpp | LOW |
| **P0** | Developer/system role merging | server-chat.cpp | LOW |
| **P0** | Responses-only key stripping | server-chat.cpp | LOW |
| **P0** | Non-function tool skipping (200 not 400) | server-chat.cpp | LOW |
| **P0** | `input_text` / `input_file` type support | server-chat.cpp | LOW |
| **P0** | Reasoning content format flexibility | server-chat.cpp | LOW |
| **P1** | `sequence_number` on streaming events | server-task.h/cpp | LOW-MED |
| **P1** | `output_index` / `content_index` on streaming | server-task.h/cpp | LOW-MED |
| **P1** | Full `response.created`/`in_progress` object | server-task.cpp | MED |
| **P1** | 24 Response object fields | server-task.cpp | MED |
| **P1** | `output_text` convenience field | server-task.cpp | LOW |
| **P1** | `no_prefill_assistant` / `unsupported_image_as_text` | server-common.h/cpp | LOW |
| **P2** | Function call `id`/`call_id` mapping | server-task.cpp | MED |
| **P2** | Consistent `fc_` IDs across streaming | server-task.h/cpp | HIGH |
| **P2** | Two-pass parser for tools+images | server-context.cpp | MED |
| **P2** | Reasoning block close logic + summary events | server-task.cpp | HIGH |
| **P2** | `phase` field on message items | server-task.cpp | LOW |
| **P2** | Tool name sanitization | server-chat.cpp | LOW |
| **P3** | MCP tool output encoding | server-chat.cpp | LOW |
| **P3** | Structured `response.failed` error SSE | server-context.cpp | LOW |
| **P3** | Helper functions refactoring | server-task.cpp, server-chat.cpp | LOW |

---

## 4. Conflict Risks with eb29686

| Area | Risk | Reason |
|------|------|--------|
| `update()` state machine | **HIGH** | Eb29686 changed diff ordering/processing; sequence number advancement and block-close logic assume specific ordering |
| Tool call assembly in server-chat.cpp | **MEDIUM** | `append_assistant_tool_call` merges into last assistant msg; eb29686 changed tool call arguments validation |
| Two-pass parser in server-context.cpp | **MEDIUM** | Depends on tool param structure; second pass reads original body before first-pass mutations |
| Output formatting helpers | **LOW** | Pure data construction, independent of generation state |
| Graceful degradation | **LOW** | All input-type handling changes are strictly more permissive |

---

## 5. Minimal Backport Order

### Phase 1 — Safe Additions (no generation logic changes)
1. `server-common.h` — add `no_prefill_assistant`, `unsupported_image_as_text` params
2. `server-common.cpp` — implement params (gated by defaults, no behavioral change)
3. `server-task.h` — add all new state fields with initializers
4. `server-chat.cpp` — graceful degradation (input type handling, key stripping, role merging)
5. `server-chat.cpp` — new helper functions (tool assembly, MCP encoding)
6. Test file + phase 1 tests

### Phase 2 — Output Formatting
7. `server-task.cpp` — helper functions: `build_oai_resp_metadata()`, `build_responses_*_item()` etc.
8. `server-task.cpp` — non-streaming response: 24 fields, `output_text`, `usage` details
9. `server-task.cpp` — streaming response: `sequence_number`, `output_index`, `content_index`

### Phase 3 — State Machine (requires testing)
10. `server-task.cpp` — `update()` state machine: seq_num, output_idx, fc_ IDs, block close
11. `server-task.cpp` — reasoning summary events, `phase` field
12. `server-context.cpp` — two-pass parser, structured error SSE

### Phase 4 — Polish
13. `server-context.cpp` — error formatting for OAI_RESP
14. Test file expansion
15. README.md (1-line deletion, optional)

---

## 6. Test Plan

### Unit tests (new file: `test_compat_oai_responses.py`)
Run with pytest against running server:
```bash
cd tools/server/tests
LLAMA_SERVER_BIN_PATH=./build/bin/llama-server pytest unit/test_compat_oai_responses.py -v
```

### Server tests (existing)
```bash
cd tools/server/tests
PORT=18080 LLAMA_SERVER_BIN_PATH=../../../build/bin/llama-server ./tests.sh
```

### Manual E2E with Codex CLI
```toml
# ~/.codex/config.toml
[model_providers]
llamacpp = { base_url = "http://localhost:8080/v1" }
```
```bash
codex --model-provider llamacpp exec -p . "echo hello"
```

### Async OpenAI SDK test
```python
from openai import AsyncOpenAI
client = AsyncOpenAI(base_url="http://localhost:8080/v1", api_key="not-needed")
resp = await client.responses.create(model="...", input="hello")
print(resp.output_text)
```

---

## 7. Cleanup Files

After analysis, remove temporary patch files:
```bash
rm pr21174_*.patch pr21174_full.patch
```
