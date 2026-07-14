# Tool Call Client Compatibility

## Incomplete Tool Call Arguments

When a model produces tool calls with incomplete or malformed JSON arguments
(e.g. due to generation being truncated by `max_tokens`, a stop word, or EOS),
llama.cpp returns `finish_reason: "length"` instead of `"tool_calls"` and adds
the debug field `__tool_call_incomplete: true`.

### Client-side requirements

Clients **must** check the validity of tool call arguments before executing them.
A tool call with incomplete arguments should never be dispatched to a tool handler.

The recommended check:

```python
import json

def is_valid_tool_call(tool_call):
    if not tool_call.get("arguments"):
        return False
    try:
        parsed = json.loads(tool_call["arguments"])
        return isinstance(parsed, dict)
    except json.JSONDecodeError:
        return False
```

Clients should also check `finish_reason`:

- `finish_reason: "length"` + non-empty `tool_calls` => potential incomplete arguments
- `__tool_call_incomplete: true` => explicitly incomplete

### Rationale

This design follows the principle that invalid tool call arguments should not
produce HTTP 500 errors nor be silently accepted. By setting
`finish_reason: "length"` and keeping the partial tool calls in the response,
the server gives clients enough information to handle the situation gracefully
(e.g. by re-asking the model or discarding the incomplete tool calls).
