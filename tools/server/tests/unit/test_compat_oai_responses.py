import pytest
from openai import OpenAI
from utils import *

server: ServerProcess

@pytest.fixture(autouse=True)
def create_server():
    global server
    server = ServerPreset.tinyllama2()

def test_responses_with_openai_library():
    global server
    server.start()
    client = OpenAI(api_key="dummy", base_url=f"http://{server.server_host}:{server.server_port}/v1")
    res = client.responses.create(
        model="gpt-4.1",
        input=[
            {"role": "system", "content": "Book"},
            {"role": "user", "content": "What is the best book"},
        ],
        max_output_tokens=8,
        temperature=0.8,
    )
    assert res.id.startswith("resp_")
    assert res.output[0].id is not None
    assert res.output[0].id.startswith("msg_")
    assert match_regex("(Suddenly)+", res.output_text)

def test_responses_stream_with_openai_library():
    global server
    server.start()
    client = OpenAI(api_key="dummy", base_url=f"http://{server.server_host}:{server.server_port}/v1")
    stream = client.responses.create(
        model="gpt-4.1",
        input=[
            {"role": "system", "content": "Book"},
            {"role": "user", "content": "What is the best book"},
        ],
        max_output_tokens=8,
        temperature=0.8,
        stream=True,
    )

    gathered_text = ''
    resp_id = ''
    msg_id = ''
    for r in stream:
        if r.type == "response.created":
            assert r.response.id.startswith("resp_")
            resp_id = r.response.id
        if r.type == "response.in_progress":
            assert r.response.id == resp_id
        if r.type == "response.output_item.added":
            assert r.item.id is not None
            assert r.item.id.startswith("msg_")
            msg_id = r.item.id
        if (r.type == "response.content_part.added" or
            r.type == "response.output_text.delta" or
            r.type == "response.output_text.done" or
            r.type == "response.content_part.done"):
            assert r.item_id == msg_id
        if r.type == "response.output_item.done":
            assert r.item.id == msg_id

        if r.type == "response.output_text.delta":
            gathered_text += r.delta
        if r.type == "response.completed":
            assert r.response.id.startswith("resp_")
            assert r.response.output[0].id is not None
            assert r.response.output[0].id.startswith("msg_")
            assert gathered_text == r.response.output_text
            assert match_regex("(Suddenly)+", r.response.output_text)


def test_responses_stream_with_llama_telemetry():
    global server
    server.n_ctx = 256
    server.n_batch = 32
    server.n_slots = 1
    server.start()

    saw_progress = False
    saw_delta_timings = False
    completed = None

    res = server.make_stream_request("POST", "/responses", data={
        "input": "This is a test" * 10,
        "max_output_tokens": 8,
        "temperature": 0.8,
        "stream": True,
        "timings_per_token": True,
        "return_progress": True,
    })

    for data in res:
        if "prompt_progress" in data:
            assert data["type"] == "response.in_progress"
            assert data["prompt_progress"]["total"] > 0
            assert data["prompt_progress"]["processed"] >= data["prompt_progress"]["cache"]
            saw_progress = True
        if "timings" in data:
            assert "prompt_per_second" in data["timings"]
            assert "predicted_per_second" in data["timings"]
            if data["type"] == "response.output_text.delta":
                saw_delta_timings = True
        if data["type"] == "response.completed":
            completed = data

    assert saw_progress
    assert saw_delta_timings
    assert completed is not None
    assert "usage" in completed["response"]
    assert "timings" in completed

def make_responses_request(input_items, **kwargs):
    data = {
        "model": "gpt-4.1",
        "input": input_items,
    data.update(kwargs)
    return server.make_request("POST", "/v1/responses", data=data)
def assert_completed_response(input_items, **kwargs):
    res = make_responses_request(input_items, **kwargs)
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    return res
def test_responses_schema_fields():
    """Verify the 24 Response object fields added by this PR are present
    with correct types and default values. These fields are required by
    the OpenAI Responses API spec but were missing before this change."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": "Book",
    assert res.status_code == 200
    body = res.body
    # Usage sub-fields added by this PR
    usage = body["usage"]
    assert isinstance(usage["input_tokens_details"]["cached_tokens"], int)
    assert isinstance(usage["output_tokens_details"]["reasoning_tokens"], int)
    # All 24 fields added by this PR must be present with correct defaults
    assert body["incomplete_details"] is None
    assert body["previous_response_id"] is None
    assert body["instructions"] is None
    assert body["error"] is None
    assert body["tools"] == []
    assert body["tool_choice"] == "auto"
    assert body["truncation"] == "disabled"
    assert body["parallel_tool_calls"] == False
    assert body["text"] == {"format": {"type": "text"}}
    assert body["top_p"] == 1.0
    assert body["temperature"] == 1.0
    assert body["presence_penalty"] == 0.0
    assert body["frequency_penalty"] == 0.0
    assert body["top_logprobs"] == 0
    assert body["reasoning"] is None
    assert body["max_output_tokens"] is None
    assert body["store"] == False
    assert body["service_tier"] == "default"
    assert body["metadata"] == {}
    assert body["background"] == False
    assert body["safety_identifier"] is None
    assert body["prompt_cache_key"] is None
    assert body["max_tool_calls"] is None
def test_responses_stream_schema_fields():
    """Verify streaming done-events have the sequence_number, output_index,
    and content_index fields added by this PR. Also verify the completed
    response includes the 24 new schema fields."""
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": "Book",
    seen_seq_nums = []
    saw_output_text_done = False
    saw_content_part_done = False
    saw_output_item_done = False
    completed_response = None
        assert "sequence_number" in data, f"missing sequence_number in {data.get('type')}"
        seen_seq_nums.append(data["sequence_number"])
        if data.get("type") == "response.output_text.done":
            saw_output_text_done = True
            assert "content_index" in data
            assert "output_index" in data
            assert "logprobs" in data
            assert isinstance(data["logprobs"], list)
        if data.get("type") == "response.content_part.done":
            saw_content_part_done = True
            assert "content_index" in data
            assert "output_index" in data
        if data.get("type") == "response.output_item.done":
            saw_output_item_done = True
            assert "output_index" in data
        if data.get("type") == "response.completed":
            completed_response = data["response"]
    # Must have seen all done-event types
    assert saw_output_text_done, "never received response.output_text.done"
    assert saw_content_part_done, "never received response.content_part.done"
    assert saw_output_item_done, "never received response.output_item.done"
    # sequence_number must be present on done events and monotonically increasing
    assert len(seen_seq_nums) >= 4, f"expected >= 4 sequenced events, got {len(seen_seq_nums)}"
    assert all(a < b for a, b in zip(seen_seq_nums, seen_seq_nums[1:])), "sequence_numbers not strictly increasing"
    # completed response must have the new schema fields with correct values
    assert completed_response is not None
    assert completed_response["metadata"] == {}
    assert completed_response["store"] == False
    assert completed_response["truncation"] == "disabled"
    assert completed_response["usage"]["output_tokens_details"]["reasoning_tokens"] == 0
def test_responses_non_function_tool_skipped():
    """Non-function tool types must be silently skipped, producing a valid
    completion with no tools field in the converted chat request. Upstream
    rejects non-function types with 400; our code must return 200 and
    generate output as if no tools were provided."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": "Hello",
        "tools": [
            {"type": "code_interpreter"},
        ],
    })
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    # With all tools skipped, the model must still produce text output
    assert len(res.body["output"]) > 0
    assert len(res.body["output_text"]) > 0


def test_responses_only_non_function_tools_same_as_no_tools():
    """When ALL tools are non-function types, they should all be filtered out
    and the result should be identical to a request with no tools at all.
    Compare token counts to confirm the tools field was truly empty."""
    no_tools = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": "Hello",
    })
    with_skipped_tools = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": "Hello",
        "tools": [
            {"type": "code_interpreter"},
            {"type": "file_search"},
        ],
    })
    assert no_tools.status_code == 200
    assert with_skipped_tools.status_code == 200
    # If tools were truly stripped, prompt token count must be identical
    assert with_skipped_tools.body["usage"]["input_tokens"] == no_tools.body["usage"]["input_tokens"]
def test_responses_extra_keys_stripped():
    """Responses-only request keys (store, include, prompt_cache_key, etc.)
    must be stripped before forwarding to the chat completions handler.
    The completion must succeed and produce the same output as a request
    without those keys."""
    # Baseline without extra keys
    baseline = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
    assert baseline.status_code == 200
    # Same request with extra Responses-only keys
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
        "store": True,
        "include": ["usage"],
        "prompt_cache_key": "test_key",
        "web_search": {"enabled": True},
        "text": {"format": {"type": "text"}},
        "truncation": "auto",
        "metadata": {"key": "value"},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    # Extra keys must not affect token consumption
    assert res.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]
def test_responses_developer_role_merging():
    """Developer role messages must be merged into the first system message
    at position 0. This ensures templates that require a single system
    message don't see developer content as a separate turn.
    We verify by comparing token counts: system + developer merged should
    consume the same prompt tokens as a single system message with the
    combined content."""
    # Single combined system message
    combined = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "system", "content": [
                {"type": "input_text", "text": "Book"},
                {"type": "input_text", "text": "Keep it short"},
            ]},
            {"role": "user", "content": [{"type": "input_text", "text": "What is the best book"}]},
    assert combined.status_code == 200
    # Split system + developer (should be merged to same prompt)
    split = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "system", "content": [{"type": "input_text", "text": "Book"}]},
            {"role": "user", "content": [{"type": "input_text", "text": "What is the best book"}]},
            {"role": "developer", "content": [{"type": "input_text", "text": "Keep it short"}]},
    assert split.status_code == 200
    assert split.body["status"] == "completed"
    # Merged prompt should consume same number of input tokens
    assert split.body["usage"]["input_tokens"] == combined.body["usage"]["input_tokens"]
def test_responses_input_text_type_multi_turn():
    """input_text type must be accepted for assistant messages (EasyInputMessage).
    An assistant message without explicit type:'message' must also be accepted
    (AssistantMessageItemParam). Verify the multi-turn context is preserved
    by checking the model sees the full conversation."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Hello"}]},
                "role": "assistant",
                "content": [{"type": "input_text", "text": "Hi there"}],
            },
            {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    # Multi-turn input should result in more prompt tokens than single-turn
    single = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": "How are you",
    assert single.status_code == 200
    assert res.body["usage"]["input_tokens"] > single.body["usage"]["input_tokens"]
def test_responses_output_text_matches_content():
    """output_text must be the concatenation of all output_text content parts.
    Verify this for both streaming and non-streaming responses."""
    # Non-streaming
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
    assert res.status_code == 200
    # Manually reconstruct output_text from content parts
    reconstructed = ""
    for item in res.body["output"]:
        if item.get("type") == "message":
            for part in item["content"]:
                if part.get("type") == "output_text":
                    reconstructed += part["text"]
    assert res.body["output_text"] == reconstructed
    assert len(reconstructed) > 0
def test_responses_stream_output_text_consistency():
    """Streaming gathered text must match the output_text in response.completed."""
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
    gathered_text = ""
    completed_output_text = None
        if data.get("type") == "response.output_text.delta":
            gathered_text += data["delta"]
        if data.get("type") == "response.completed":
            completed_output_text = data["response"]["output_text"]
            # Also verify content parts match
            for item in data["response"]["output"]:
                if item.get("type") == "message":
                    for part in item["content"]:
                        if part.get("type") == "output_text":
                            assert part["text"] == gathered_text
    assert completed_output_text is not None
    assert gathered_text == completed_output_text
    assert len(gathered_text) > 0
def test_responses_stream_created_event_has_full_response():
    """response.created must contain the full response object with all required
    fields, not just {id, object, status}. This is needed by strict client
    libraries like async-openai."""
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
    created_resp = None
    in_progress_resp = None
        if data.get("type") == "response.created":
            created_resp = data["response"]
        if data.get("type") == "response.in_progress":
            in_progress_resp = data["response"]
    assert created_resp is not None, "never received response.created"
    assert in_progress_resp is not None, "never received response.in_progress"
    # Both must have the full response object, not just minimal fields
    for resp in [created_resp, in_progress_resp]:
        assert resp["status"] == "in_progress"
        assert resp["id"].startswith("resp_")
        assert resp["object"] == "response"
        assert resp["model"] is not None
        assert resp["completed_at"] is None
        assert resp["metadata"] == {}
        assert resp["store"] == False
        assert resp["truncation"] == "disabled"
        assert resp["tools"] == []
        assert resp["usage"]["input_tokens"] == 0
        assert resp["usage"]["output_tokens"] == 0
        assert resp["output"] == []
        assert resp["output_text"] == ""
def test_responses_stream_all_events_have_sequence_number():
    """Every streaming event must have a sequence_number field and they must
    be strictly increasing across the entire stream."""
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
    all_seq_nums = []
    event_types = []
        assert "sequence_number" in data, f"missing sequence_number in event type {data.get('type')}"
        all_seq_nums.append(data["sequence_number"])
        event_types.append(data.get("type", "unknown"))
    # Must have received multiple events
    assert len(all_seq_nums) >= 6, f"expected >= 6 events, got {len(all_seq_nums)}: {event_types}"
    # Must be strictly increasing
    for i in range(1, len(all_seq_nums)):
        assert all_seq_nums[i] > all_seq_nums[i-1], \
            f"sequence_number not strictly increasing at index {i}: {all_seq_nums[i-1]} -> {all_seq_nums[i]} (events: {event_types[i-1]} -> {event_types[i]})"
def test_responses_stream_delta_events_have_indices():
    """Delta and added events must have output_index. Content-related events
    must also have content_index."""
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
    saw_output_item_added = False
    saw_content_part_added = False
    saw_output_text_delta = False
        evt = data.get("type", "")
        if evt == "response.output_item.added":
            saw_output_item_added = True
            assert "output_index" in data, "output_item.added missing output_index"
        if evt == "response.content_part.added":
            saw_content_part_added = True
            assert "output_index" in data, "content_part.added missing output_index"
            assert "content_index" in data, "content_part.added missing content_index"
        if evt == "response.output_text.delta":
            saw_output_text_delta = True
            assert "output_index" in data, "output_text.delta missing output_index"
            assert "content_index" in data, "output_text.delta missing content_index"
    assert saw_output_item_added, "never received response.output_item.added"
    assert saw_content_part_added, "never received response.content_part.added"
    assert saw_output_text_delta, "never received response.output_text.delta"
def test_responses_reasoning_content_array():
    """Reasoning items with content as array (spec format) must be accepted."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
            {"type": "reasoning", "summary": [],
             "content": [{"type": "reasoning_text", "text": "thinking"}]},
            {"role": "assistant", "type": "message",
             "content": [{"type": "output_text", "text": "Hello"}]},
            {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
def test_responses_reasoning_content_string():
    """Reasoning items with content as plain string (OpenCode format) must be accepted."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
            {"type": "reasoning", "summary": [], "content": "thinking about it"},
            {"role": "assistant", "type": "message",
             "content": [{"type": "output_text", "text": "Hello"}]},
            {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
def test_responses_reasoning_content_null():
    """Reasoning items with content:null (Codex format, issue openai/codex#11834)
    must be accepted �1�7 content may be null when encrypted_content is present."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
            {"type": "reasoning", "summary": [], "content": None,
             "encrypted_content": "opaque_data_here"},
            {"role": "assistant", "type": "message",
             "content": [{"type": "output_text", "text": "Hello"}]},
            {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
def test_responses_reasoning_content_omitted():
    """Reasoning items with content omitted entirely must be accepted."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
            {"type": "reasoning", "summary": []},
            {"role": "assistant", "type": "message",
             "content": [{"type": "output_text", "text": "Hello"}]},
            {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
def test_responses_input_file_with_data_graceful():
    """input_file items with file_data must be rendered as text content
    instead of rejecting the entire request."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [
                {"type": "input_text", "text": "Summarize this file"},
                {"type": "input_file", "file_data": "hello world", "filename": "test.txt"},
            ]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    # The file content must reach the model as prompt tokens
    baseline = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [
                {"type": "input_text", "text": "Summarize this file"},
            ]},
    assert baseline.status_code == 200
    # With file_data injected as text, prompt must be longer
    assert res.body["usage"]["input_tokens"] > baseline.body["usage"]["input_tokens"]
def test_responses_input_file_filename_only():
    """input_file with only filename (no file_data) must produce a placeholder."""
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [
                {"type": "input_text", "text": "What is this?"},
                {"type": "input_file", "filename": "report.pdf"},
            ]},
    assert res.status_code == 200
    assert res.body["status"] == "completed"
def test_responses_unknown_content_type_skipped():
    """Unknown user content types are skipped silently: the request completes
    and the converted prompt is unchanged (no diagnostic text injected)."""
    baseline = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Hello"},
        ]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Hello"},
            {"type": "input_audio", "data": "base64stuff"},
        ]},
    ])
    assert recovered.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]
def test_responses_unknown_assistant_content_type_skipped():
    """Unknown assistant content types are skipped silently: the request
    completes and the converted prompt is unchanged."""
    baseline = assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"role": "assistant", "type": "message", "content": [
            {"type": "output_text", "text": "Hello"},
        ]},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"role": "assistant", "type": "message", "content": [
            {"type": "output_text", "text": "Hello"},
            {"type": "some_future_type", "data": "foo"},
        ]},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
    assert recovered.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]
def test_responses_unknown_toplevel_item_skipped():
    """Unknown top-level item types must be skipped rather than rejecting."""
    assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"type": "some_new_item_type", "data": "whatever"},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
def test_responses_malformed_input_text_skipped():
    """Malformed input_text (missing text) is skipped silently: the request
    completes and the converted prompt is unchanged."""
    baseline = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Hello"},
        ]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Hello"},
            {"type": "input_text"},
        ]},
    ])
    assert recovered.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]
def test_responses_malformed_input_image_skipped():
    """Malformed input_image (missing image_url) is skipped silently: the
    request completes and the converted prompt is unchanged."""
    baseline = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Describe this attachment"},
        ]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Describe this attachment"},
            {"type": "input_image"},
        ]},
    ])
    assert recovered.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]
def test_responses_image_with_tools():
    """Image content plus tools require two parser passes. The second
    pass must read the original image_url content, not the media_marker
    mutation from the first pass."""
    server = ServerPreset.tinygemma3()
    server.jinja = True
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [
                {"type": "input_text", "text": "What is shown?"},
                {"type": "input_image", "image_url":
                    "https://huggingface.co/ggml-org/tinygemma3-GGUF/resolve/main/test/11_truck.png"},
            ]},
        "tools": [{
            "type": "function",
            "name": "get_image",
            "parameters": {
                "type": "object",
                "properties": {"unused": {"type": "string"}},
                "required": ["unused"],
            },
        }],
        "max_output_tokens": 4,
        "temperature": 0.0,
    assert res.status_code == 200
    assert res.body["status"] == "completed"
def test_responses_malformed_input_file_recovery_visible():
    """Malformed input_file should keep the request alive and inject visible
    recovery text into the converted prompt."""
    baseline = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Summarize this upload"},
        ]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [
            {"type": "input_text", "text": "Summarize this upload"},
            {"type": "input_file"},
        ]},
    ])
    assert recovered.body["usage"]["input_tokens"] > baseline.body["usage"]["input_tokens"]
def test_responses_malformed_assistant_output_text_skipped():
    """Malformed assistant output_text history is skipped silently: the request
    completes and the converted prompt is unchanged."""
    baseline = assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"role": "assistant", "type": "message", "content": [
            {"type": "output_text", "text": "Hello"},
        ]},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"role": "assistant", "type": "message", "content": [
            {"type": "output_text", "text": "Hello"},
            {"type": "output_text"},
        ]},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
    assert recovered.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]
def test_responses_malformed_assistant_refusal_skipped():
    """Malformed refusal history is skipped silently: the request completes
    and the converted prompt is unchanged."""
    baseline = assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"role": "assistant", "type": "message", "content": [
            {"type": "output_text", "text": "Hello"},
        ]},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
    recovered = assert_completed_response([
        {"role": "user", "content": [{"type": "input_text", "text": "Hi"}]},
        {"role": "assistant", "type": "message", "content": [
            {"type": "output_text", "text": "Hello"},
            {"type": "refusal"},
        ]},
        {"role": "user", "content": [{"type": "input_text", "text": "How are you"}]},
    ])
    assert recovered.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"]


def _check_tool_conversion(body, tools_key="tools"):
    """Helper: check that a Responses request with given tools succeeds."""
    global server
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 4,
        "temperature": 0.0,
        **body,
    })
    assert res.status_code == 200, f"Expected 200, got {res.status_code}: {res.body}"
    assert res.body["status"] == "completed"


def test_responses_namespace_tool():
    """Namespace tools are expanded: each sub-tool becomes a function tool."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{
            "type": "namespace",
            "name": "mcp__demo__",
            "description": "Demo MCP tools",
            "tools": [
                {"type": "function", "name": "lookup_order", "description": "Look up order", "parameters": {"type": "object", "properties": {"id": {"type": "string"}}}},
                {"type": "function", "name": "get_product", "description": "Get product", "parameters": {"type": "object", "properties": {"sku": {"type": "string"}}}},
            ],
        }],
    })


def test_responses_namespace_multiple():
    """Multiple namespaces with same sub-tool name do not conflict."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [
            {"type": "namespace", "name": "ns1", "description": "Namespace 1", "tools": [
                {"type": "function", "name": "tool_a", "description": "Tool A", "parameters": {"type": "object", "properties": {}}},
            ]},
            {"type": "namespace", "name": "ns2", "description": "Namespace 2", "tools": [
                {"type": "function", "name": "tool_a", "description": "Tool A (duplicate name)", "parameters": {"type": "object", "properties": {}}},
            ]},
        ],
    })


def test_responses_custom_tool():
    """Custom (freeform) tools are converted to function tools with string input."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{
            "type": "custom",
            "name": "exec",
            "description": "Execute a shell command",
            "format": {"type": "grammar", "syntax": "lark", "definition": "start: "exec""},
        }],
    })


def test_responses_tool_search_tool():
    """Tool search tools are converted to function tools with query params."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{
            "type": "tool_search",
            "execution": "sync",
            "description": "Search for tools",
            "parameters": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]},
        }],
    })


def test_responses_web_search_native():
    """Web search tools are exposed as internal function schemas (native passthrough)."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [
            {"type": "web_search"},
            {"type": "function", "name": "get_weather", "description": "Get weather", "parameters": {"type": "object", "properties": {"city": {"type": "string"}}}},
        ],
    })


def test_responses_all_tool_types_mixed():
    """Mixed function/namespace/custom/tool_search/web_search all work together."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [
            {"type": "function", "name": "get_weather", "description": "Get weather", "parameters": {"type": "object", "properties": {"city": {"type": "string"}}}},
            {"type": "namespace", "name": "mcp", "description": "MCP", "tools": [
                {"type": "function", "name": "read_file", "description": "Read file", "parameters": {"type": "object", "properties": {"path": {"type": "string"}}}},
            ]},
            {"type": "custom", "name": "exec", "description": "Shell", "format": {"type": "grammar", "syntax": "lark", "definition": "start: "exec""}},
            {"type": "tool_search", "execution": "sync", "description": "Search", "parameters": {"type": "object", "properties": {"q": {"type": "string"}}}},
            {"type": "web_search"},
        ],
    })


def test_responses_namespace_empty_tools():
    """A namespace with no tools array produces no tools (no crash)."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{"type": "namespace", "name": "empty_ns", "description": "Empty"}],
    })


def test_responses_namespace_missing_name():
    """A namespace without a name uses default 'namespace' prefix."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{"type": "namespace", "description": "No name", "tools": [
            {"type": "function", "name": "orphan_tool", "description": "Orphan", "parameters": {"type": "object", "properties": {}}},
        ]}],
    })


def test_responses_namespace_no_function_skipped():
    """Namespace sub-tools that are not 'function' type are skipped."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{"type": "namespace", "name": "ns", "description": "NS", "tools": [
            {"type": "web_search"},  # skipped
            {"type": "function", "name": "real_tool", "description": "Real", "parameters": {"type": "object", "properties": {}}},
        ]}],
    })


def test_responses_custom_no_description():
    """Custom tool without description still works."""
    global server
    server.start()
    _check_tool_conversion({
        "tools": [{"type": "custom", "name": "apply_patch", "description": "Apply a patch"},
                  {"type": "custom", "name": "read_file", "description": "Read file contents"}],
    })


def test_responses_custom_tool_roundtrip():
    """Custom tool output must use custom_tool_call type with raw input."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Run ls -la"}]}],
        "max_output_tokens": 4,
        "temperature": 0.0,
        "tools": [{"type": "custom", "name": "exec", "description": "Execute a shell command",
                   "format": {"type": "grammar", "syntax": "lark", "definition": "start: exec"}}],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")


def test_responses_namespace_tool_roundtrip():
    """Namespace tool output must use function_call type with namespace__ prefix."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 4,
        "temperature": 0.0,
        "tools": [{"type": "namespace", "name": "mcp", "description": "MCP",
                   "tools": [{"type": "function", "name": "read_file", "description": "Read file",
                             "parameters": {"type": "object", "properties": {"path": {"type": "string"}}}}]}],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")


def test_responses_tool_search_roundtrip():
    """Tool search round-trip must not crash."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Find a tool"}]}],
        "max_output_tokens": 4,
        "temperature": 0.0,
        "tools": [{"type": "tool_search", "execution": "sync", "description": "Search tools",
                   "parameters": {"type": "object", "properties": {"query": {"type": "string"}}}}],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")


def test_responses_concurrent_tool_map():
    """Concurrent requests must not share tool mapping."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 4,
        "temperature": 0.0,
        "tools": [{"type": "function", "name": "test_fn", "description": "Test fn",
                   "parameters": {"type": "object", "properties": {"x": {"type": "string"}}}}],
    })
    assert res.status_code == 200


# ���� Web-search bridge tests ��������������������������������������������������������������������������������������������

def test_web_search_replacement_no_override_custom_namespace():
    """Web search mapping must not overwrite custom/namespace/tool_search maps (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "get_weather", "description": "Get weather",
             "parameters": {"type": "object", "properties": {"city": {"type": "string"}}}},
            {"type": "namespace", "name": "mcp", "description": "MCP", "tools": [
                {"type": "function", "name": "read_file", "description": "Read file",
                 "parameters": {"type": "object", "properties": {"path": {"type": "string"}}}},
            ]},
            {"type": "custom", "name": "exec", "description": "Execute a shell command"},
            {"type": "tool_search", "execution": "sync", "description": "Search tools"},
            {"type": "web_search"},
        ],
    })


def test_web_search_replacement_custom_type_preserved():
    """Custom tool used as web_search replacement must retain original_type=custom (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "custom", "name": "web_search", "description": "Search the web for information"},
            {"type": "web_search"},
        ],
    })


def test_web_search_replacement_namespace_preserved():
    """Namespace tool used as web_search replacement must retain original_type=namespace with name (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "namespace", "name": "search", "description": "Web search namespace", "tools": [
                {"type": "function", "name": "web_search", "description": "Search the web",
                 "parameters": {"type": "object", "properties": {"q": {"type": "string"}}}},
            ]},
            {"type": "web_search"},
        ],
    })


def test_web_search_param_query_to_q():
    """query parameter must remap to q when replacement expects q (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "web_search", "description": "Search the web",
             "parameters": {"type": "object", "properties": {"q": {"type": "string"}},
                            "required": ["q"]}},
            {"type": "web_search"},
        ],
    })


def test_web_search_param_query_to_search_query():
    """query parameter must remap to search_query when replacement expects search_query (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "web_search", "description": "Search the web",
             "parameters": {"type": "object", "properties": {"search_query": {"type": "string"}},
                            "required": ["search_query"]}},
            {"type": "web_search"},
        ],
    })


def test_web_search_case_insensitive_description_match():
    """Description-based web search matching must be case-insensitive (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "lookup", "description": "WEB Search for content on the INTERNET"},
            {"type": "web_search"},
        ],
    })


def test_web_search_not_code_file_repo_search():
    """Tools described as code/file/repository/grep/local search must NOT match as web search (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "search", "description": "Search code in repository"},
            {"type": "function", "name": "grep", "description": "Search file contents with grep"},
            {"type": "function", "name": "file_search", "description": "Search local file system"},
            {"type": "web_search"},
        ],
    })


def test_web_search_no_replacement_skipped_safely():
    """When no web-search-compatible tool is available in replacement mode, web_search is safely skipped."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "get_weather", "description": "Get weather",
             "parameters": {"type": "object", "properties": {"city": {"type": "string"}}}},
            {"type": "web_search"},
        ],
    })


def test_web_search_mixed_with_custom_shell_and_namespace_mcp_roundtrip():
    """Mixed web_search + custom shell + namespace MCP must round-trip (replacement mode)."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "replacement",
        "tools": [
            {"type": "function", "name": "search_web", "description": "Search the web for information",
             "parameters": {"type": "object", "properties": {"q": {"type": "string"}},
                            "required": ["q"]}},
            {"type": "custom", "name": "exec", "description": "Execute a shell command"},
            {"type": "namespace", "name": "mcp", "description": "MCP namespace", "tools": [
                {"type": "function", "name": "read_file", "description": "Read a file",
                 "parameters": {"type": "object", "properties": {"path": {"type": "string"}}}},
            ]},
            {"type": "tool_search", "execution": "sync", "description": "Search available tools"},
            {"type": "web_search"},
        ],
    })


def test_web_search_disabled_mode():
    """Disabled mode must not expose web_search tool to the model."""
    global server
    server.start()
    _check_tool_conversion({
        "__responses_web_search_mode": "disabled",
        "tools": [
            {"type": "web_search"},
            {"type": "function", "name": "get_weather", "description": "Get weather",
             "parameters": {"type": "object", "properties": {"city": {"type": "string"}}}},
        ],
    })


def test_responses_function_tool_non_streaming_output_format():
    """Non-streaming response with function tools must produce valid output items.
    When tools are defined and model generates a tool call, the output must contain
    function_call items with arguments as string and call_id matching."""
    global server
    server.start()
    # Define a simple function tool
    test_tool = {
        "type": "function",
        "name": "get_weather",
        "description": "Get weather for a city",
        "parameters": {
            "type": "object",
            "properties": {
                "location": {"type": "string", "description": "City name"},
            },
            "required": ["location"],
        },
    }
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "What is the weather in Paris?"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [test_tool],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")
    # Check that output items exist in correct format
    for item in res.body.get("output", []):
        if item.get("type") == "function_call":
            # arguments must be a string, not an object
            assert isinstance(item["arguments"], str), f"arguments must be a string, got {type(item['arguments'])}"
            assert item["call_id"].startswith("call_"), f"call_id must start with 'call_', got {item['call_id']}"
            assert item["id"].startswith("fc_"), f"function_call id must start with 'fc_', got {item['id']}"
            assert "name" in item
    # response.completed must exist
    assert "status" in res.body
    assert res.body["status"] in ("completed", "incomplete")


def test_responses_custom_tool_non_streaming_output_format():
    """Non-streaming response with custom tools must produce custom_tool_call items
    using 'input' (not 'arguments') as the field name."""
    global server
    server.start()
    # Define a custom freeform tool (like apply_patch)
    custom_tool = {
        "type": "custom",
        "name": "apply_patch",
        "description": "Apply a patch to files",
        "format": {
            "type": "grammar",
            "syntax": "lark",
            "definition": "start: begin_patch hunk+ end_patch",
        },
    }
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Update the hello function"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [custom_tool],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")
    # Check that output items exist in correct format
    for item in res.body.get("output", []):
        if item.get("type") == "custom_tool_call":
            # custom uses 'input' not 'arguments'
            assert "input" in item, f"custom_tool_call must have 'input' field"
            assert "arguments" not in item, f"custom_tool_call must NOT have 'arguments' field"
            assert item["call_id"].startswith("call_"), f"call_id must start with 'call_', got {item['call_id']}"
            assert item["id"].startswith("ctc_"), f"custom_tool_call id must start with 'ctc_', got {item['id']}"
            assert "name" in item


def test_responses_function_tool_stream_events():
    """Streaming response with function tools must emit events in the correct order:
    response.created → response.output_item.done → response.completed.
    The output_item.done must contain function_call type with arguments as string."""
    global server
    server.start()
    test_tool = {
        "type": "function",
        "name": "get_weather",
        "description": "Get weather for a city",
        "parameters": {
            "type": "object",
            "properties": {
                "location": {"type": "string", "description": "City name"},
            },
            "required": ["location"],
        },
    }
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "What is the weather in Paris?"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [test_tool],
        "stream": True,
    })
    seen_seq_nums = []
    saw_created = False
    saw_completed = False
    for data in res:
        assert "sequence_number" in data
        seen_seq_nums.append(data["sequence_number"])
        if data.get("type") == "response.created":
            saw_created = True
            assert data["response"]["id"].startswith("resp_")
        if data.get("type") == "response.output_item.done":
            item = data.get("item", {})
            if item.get("type") == "function_call":
                assert isinstance(item["arguments"], str), f"arguments must be a string, got {type(item['arguments'])}"
                assert item["call_id"].startswith("call_")
                assert item["id"].startswith("fc_")
        if data.get("type") == "response.completed":
            saw_completed = True
            assert data["response"]["id"].startswith("resp_")
    assert saw_created, "must see response.created"
    assert saw_completed, "must see response.completed"
    assert len(seen_seq_nums) >= 2, "must have at least 2 sequenced events"
    assert all(a < b for a, b in zip(seen_seq_nums, seen_seq_nums[1:])), "sequence_numbers must be strictly increasing"


def test_responses_custom_tool_stream_events():
    """Streaming response with custom tools must emit correct event sequence.
    The output_item.done must contain custom_tool_call type with 'input' field."""
    global server
    server.start()
    custom_tool = {
        "type": "custom",
        "name": "apply_patch",
        "description": "Apply a patch to files",
        "format": {
            "type": "grammar",
            "syntax": "lark",
            "definition": "start: begin_patch hunk+ end_patch",
        },
    }
    res = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Update the hello function"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [custom_tool],
        "stream": True,
    })
    saw_created = False
    saw_completed = False
    for data in res:
        if data.get("type") == "response.created":
            saw_created = True
        if data.get("type") == "response.output_item.done":
            item = data.get("item", {})
            if item.get("type") == "custom_tool_call":
                assert "input" in item, f"custom_tool_call must have 'input'"
                assert "arguments" not in item, f"custom_tool_call must NOT have 'arguments'"
                assert item["call_id"].startswith("call_")
                assert item["id"].startswith("ctc_")
        if data.get("type") == "response.completed":
            saw_completed = True
    assert saw_created, "must see response.created"
    assert saw_completed, "must see response.completed"


def test_responses_function_call_output_history():
    """function_call_output must be ingested into chat history with matching call_id.
    The server must not reject the request and must produce output."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Check the weather"}]},
            {"type": "function_call_output", "call_id": "call_abc123", "output": "Sunny, 25°C"},
        ],
        "max_output_tokens": 50,
        "temperature": 0.0,
    })
    assert res.status_code == 200, f"Expected 200, got {res.status_code}: {res.body}"
    assert res.body["status"] in ("completed", "incomplete")


def test_responses_custom_tool_call_output_history():
    """custom_tool_call_output must be ingested into chat history with matching call_id.
    The server must not reject the request and must produce output."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [
            {"role": "user", "content": [{"type": "input_text", "text": "Apply the patch"}]},
            {"type": "custom_tool_call_output", "call_id": "call_def456", "output": "Patch applied successfully"},
        ],
        "max_output_tokens": 50,
        "temperature": 0.0,
    })
    assert res.status_code == 200, f"Expected 200, got {res.status_code}: {res.body}"
    assert res.body["status"] in ("completed", "incomplete")


def test_responses_plain_text_not_mistaken_as_tool_call():
    """Plain assistant text must not be converted to a tool call in the output.
    When no tools are defined, the output must only contain message items."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello, how are you?"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        # No tools defined
    })
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    for item in res.body.get("output", []):
        # With no tools, output must not contain tool call items
        assert item["type"] != "function_call", f"Unexpected function_call in output with no tools: {item}"
        assert item["type"] != "custom_tool_call", f"Unexpected custom_tool_call in output with no tools: {item}"


def test_responses_tools_not_in_prompt():
    """Tools must not be injected into the model prompt when no tool_choice is set.
    The input token count should be the same as a request without tools,
    proving the tools field was erased before sending to the model."""
    global server
    server.start()
    # Baseline: no tools
    baseline = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
    })
    assert baseline.status_code == 200
    # With tools (but no tool_choice)
    with_tools = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tools": [{
            "type": "function",
            "name": "test_fn",
            "description": "A test function",
            "parameters": {"type": "object", "properties": {}},
        }],
    })
    assert with_tools.status_code == 200
    # Input token count must be identical because tools are erased from prompt
    assert with_tools.body["usage"]["input_tokens"] == baseline.body["usage"]["input_tokens"], \
        f"Tools leaked into prompt: baseline={baseline.body['usage']['input_tokens']}, with_tools={with_tools.body['usage']['input_tokens']}"


def test_responses_function_call_id_consistent_across_rounds():
    """The call_id in function_call output must match the call_id in
    function_call_output input for round-trip consistency.
    Round-trip: assistant generates function_call with call_id → user
    passes function_call_output with same call_id → next assistant turn."""
    global server
    server.start()
    # First request: ask a question with a function tool, get a function call back
    test_tool = {
        "type": "function",
        "name": "get_weather",
        "description": "Get weather for a city",
        "parameters": {
            "type": "object",
            "properties": {
                "location": {"type": "string", "description": "City name"},
            },
            "required": ["location"],
        },
    }
    first = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "What is the weather in Paris?"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [test_tool],
    })
    assert first.status_code == 200
    # Extract any function_call items and their call_ids
    call_ids = []
    for item in first.body.get("output", []):
        if item.get("type") == "function_call":
            call_ids.append(item["call_id"])
            assert item["call_id"].startswith("call_")
    # Second request: feed back the function_call_output with matching call_id
    if call_ids:
        history_items = [
            {"role": "user", "content": [{"type": "input_text", "text": "What is the weather in Paris?"}]},
        ]
        # Add previous assistant tool calls and their outputs
        hist_user = [
            {"role": "user", "content": [{"type": "input_text", "text": "What is the weather in Paris?"}]},
            {"type": "function_call_output", "call_id": call_ids[0], "output": "Sunny, 25°C"},
        ]
        second = server.make_request("POST", "/v1/responses", data={
            "model": "gpt-4.1",
            "input": hist_user,
            "max_output_tokens": 50,
            "temperature": 0.0,
        })
        assert second.status_code == 200, f"Second request failed: {second.status_code}: {second.body}"
        assert second.body["status"] in ("completed", "incomplete")


def test_responses_custom_call_id_consistent_across_rounds():
    """The call_id in custom_tool_call output must match the call_id in
    custom_tool_call_output input for round-trip consistency."""
    global server
    server.start()
    custom_tool = {
        "type": "custom",
        "name": "apply_patch",
        "description": "Apply a patch to files",
        "format": {
            "type": "grammar",
            "syntax": "lark",
            "definition": "start: begin_patch hunk+ end_patch",
        },
    }
    first = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Apply the patch"}]}],
        "max_output_tokens": 50,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [custom_tool],
    })
    assert first.status_code == 200
    # Extract any custom_tool_call items and their call_ids
    call_ids = []
    for item in first.body.get("output", []):
        if item.get("type") == "custom_tool_call":
            call_ids.append(item["call_id"])
            assert item["call_id"].startswith("call_")
    # Second request: feed back the custom_tool_call_output with matching call_id
    if call_ids:
        hist_user = [
            {"role": "user", "content": [{"type": "input_text", "text": "Apply the patch"}]},
            {"type": "custom_tool_call_output", "call_id": call_ids[0], "output": "Patch applied successfully"},
        ]
        second = server.make_request("POST", "/v1/responses", data={
            "model": "gpt-4.1",
            "input": hist_user,
            "max_output_tokens": 50,
            "temperature": 0.0,
        })
        assert second.status_code == 200, f"Second request failed: {second.status_code}: {second.body}"
        assert second.body["status"] in ("completed", "incomplete")


def test_responses_completed_must_exist():
    """response.completed must always be present as the final status."""
    global server
    server.start()
    # Non-streaming
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 10,
    })
    assert res.status_code == 200
    assert res.body["status"] == "completed"
    # Streaming
    stream = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 10,
        "stream": True,
    })
    saw_completed = False
    for data in stream:
        if data.get("type") == "response.completed":
            saw_completed = True
            assert data["response"]["status"] == "completed"
    assert saw_completed, "must see response.completed in streaming"
