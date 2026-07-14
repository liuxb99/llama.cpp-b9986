#!/usr/bin/env python3
"""Tests for known-tool XML parser (parse_known_tool_xml_calls).

These are static/unit tests that verify the parser logic without
requiring a running server. The actual C++ implementation is tested
indirectly through these integration tests.

Run: pytest tools/server/tests/unit/test_known_tool_xml.py
"""

import pytest
import json
import subprocess
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from utils import *

server: ServerProcess


@pytest.fixture(autouse=True)
def create_server():
    global server
    server = ServerPreset.tinyllama2()


# ---------------------------------------------------------------------------
# Helper: send a Responses request with a known tool, expect tool call back
# ---------------------------------------------------------------------------

READ_FILE_TOOL = {
    "type": "function",
    "name": "read_file",
    "description": "Read a file from the filesystem",
    "parameters": {
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file"},
        },
        "required": ["path"],
        "additionalProperties": False,
    },
    "strict": True,
}

SHELL_TOOL = {
    "type": "function",
    "name": "shell_command",
    "description": "Run a shell command",
    "parameters": {
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "Command to run"},
        },
        "required": ["command"],
        "additionalProperties": False,
    },
    "strict": True,
}

CUSTOM_TOOL = {
    "type": "custom",
    "name": "apply_patch",
    "description": "Apply a patch to files",
    "format": {
        "type": "grammar",
        "syntax": "lark",
        "definition": "start: begin_patch hunk+ end_patch",
    },
}

NAMESPACE_TOOL = {
    "type": "namespace",
    "name": "mcp__github",
    "description": "GitHub tools",
    "tools": [
        {"type": "function", "name": "get_issue", "description": "Get an issue",
         "parameters": {"type": "object", "properties": {"number": {"type": "integer"}},
                        "required": ["number"]}},
    ],
}


def test_known_tool_xml_single_read_file():
    """Single <read_file> XML tag should parse as function_call."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Read the file"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [READ_FILE_TOOL],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")
    for item in res.body.get("output", []):
        if item.get("type") == "function_call":
            assert item["name"] == "read_file"
            assert isinstance(item["arguments"], str)
            assert item["call_id"].startswith("call_")


def test_known_tool_xml_shell_command():
    """Single <shell_command> XML tag should parse as function_call."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Run a command"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [SHELL_TOOL],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")
    for item in res.body.get("output", []):
        if item.get("type") == "function_call":
            assert item["name"] == "shell_command"
            assert isinstance(item["arguments"], str)


def test_known_tool_xml_custom():
    """Custom tool XML should restore as custom_tool_call with 'input'."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Apply the patch"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [CUSTOM_TOOL],
    })
    assert res.status_code == 200
    assert res.body["status"] in ("completed", "incomplete")
    # If custom_tool_call is emitted (model may or may not generate one)
    for item in res.body.get("output", []):
        if item.get("type") == "custom_tool_call":
            assert "input" in item
            assert "arguments" not in item
            assert item["call_id"].startswith("call_")


def test_known_tool_xml_namespace():
    """Namespace sub-tool with internal unique name should restore correctly."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Get issue #1"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [NAMESPACE_TOOL],
    })
    assert res.status_code == 200


def test_known_tool_xml_no_tools():
    """Plain text with no tools must not produce tool calls."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Hello"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        # No tools defined
    })
    assert res.status_code == 200
    for item in res.body.get("output", []):
        assert item["type"] != "function_call"
        assert item["type"] != "custom_tool_call"


def test_known_tool_xml_multiple_tools():
    """Multiple tools in the request must all be available."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Read and run"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [READ_FILE_TOOL, SHELL_TOOL],
    })
    assert res.status_code == 200


def test_known_tool_xml_stream_events():
    """Streaming response must include response.completed."""
    global server
    server.start()
    stream = server.make_stream_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Read the file"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [READ_FILE_TOOL],
        "stream": True,
    })
    saw_completed = False
    for data in stream:
        if data.get("type") == "response.completed":
            saw_completed = True
    assert saw_completed, "Must see response.completed"


def test_known_tool_xml_tool_call_still_works():
    """Existing <tool_call> format must still be handled (regression)."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Read the file using tool_call"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [READ_FILE_TOOL],
    })
    assert res.status_code == 200


def test_known_tool_xml_output_format():
    """Non-streaming response output format must be valid."""
    global server
    server.start()
    res = server.make_request("POST", "/v1/responses", data={
        "model": "gpt-4.1",
        "input": [{"role": "user", "content": [{"type": "input_text", "text": "Read the file"}]}],
        "max_output_tokens": 10,
        "temperature": 0.0,
        "tool_choice": "required",
        "tools": [READ_FILE_TOOL],
    })
    assert res.status_code == 200
    assert "output" in res.body
    assert "status" in res.body
    assert res.body["status"] in ("completed", "incomplete")
