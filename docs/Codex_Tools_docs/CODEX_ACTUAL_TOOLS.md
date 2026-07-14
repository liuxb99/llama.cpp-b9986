# Codex Tools Dump

## Overview
- **Hash**: `658ff3c4`
- **Captured At**: 2026-07-14T17:35:44
- **Top-level Tools**: 12
- **Expanded (with sub-tools)**: 12

## Tool Counts by Type
| Type | Count |
|------|-------|
| function | 9 |
| custom | 1 |
| namespace | 1 |
| namespace_subtools | 1 |
| tool_search | 1 |
| web_search | 0 |
| other | 0 |

## Tool Details

### 1. shell_command (function)
- **Sanitized Name**: `shell_command`
- **Map Key**: `shell_command`
- **Original Type**: `function`
- **Original Name**: `shell_command`
- **Description**: Runs a Powershell command (Windows) and returns its output.

Examples of valid command strings:

- ls -a (show hidden): "Get-ChildItem -Force"
- recursive find by name: "Get-ChildItem -Recurse -Filter *.py"
- recursive grep: "Get-ChildItem -Path C:\\myrepo -Recurse | Select-String -Pattern 'TODO' -CaseSensitive"
- ps aux | grep python: "Get-Process | Where-Object { $_.ProcessName -like '*python*' }"
- setting an env var: "$env:FOO='bar'; echo $env:FOO"
- running an inline Python script: "@'\\nprint('Hello, world!')\\n'@ | python -"

Windows safety rules:
- Do not compose destructive filesystem commands across shells. Do not enumerate paths in PowerShell and then pass them to `cmd /c`, batch builtins, or another shell for deletion or moving. Use one shell end-to-end, prefer native PowerShell cmdlets such as `Remove-Item` / `Move-Item` with `-LiteralPath`, and avoid string-built shell commands for file operations.
- Before any recursive delete or move on Windows, verify the resolved absolute target paths stay within the intended workspace or explicitly named target directory. Never issue a recursive delete or move against a computed path if the final target has not been checked.
- When using `Start-Process` to launch a background helper or service, pass `-WindowStyle Hidden` unless the user explicitly asked for a visible interactive window. Use visible windows only for interactive tools the user needs to see or control.
- **Parameters**: {"type":"object","properties":{"command":{"type":"string","description":"Shell script to run in the user's default shell."},"justification":{"type":"string","description":"User-facing approval question for `require_escalated`; omit otherwise."},"login":{"type":"boolean","description":"True runs with login shell semantics; false disables them. Defaults to true."},"prefix_rule":{"type":"array","description":"Reusable approval prefix for `cmd`, only with `sandbox_permissions: \"require_escalated\"`; for example [\"git\", \"pull\"].","items":{"type":"string"}},"sandbox_permissions":{"type":"string","description":"Per-command sandbox override. Defaults to `use_default`; use `require_escalated` for unsandboxed execution.","enum":["use_default","require_escalated"]},"timeout_ms":{"type":"number","description":"Maximum command runtime. Defaults to 10000 ms."},"workdir":{"type":"string","description":"Working directory for the command. Defaults to the turn cwd."}},"required":["command"],"additionalProperties":false}

### 2. list_mcp_resources (function)
- **Sanitized Name**: `list_mcp_resources`
- **Map Key**: `list_mcp_resources`
- **Original Type**: `function`
- **Original Name**: `list_mcp_resources`
- **Description**: Lists resources provided by MCP servers. Resources allow servers to share data that provides context to language models, such as files, database schemas, or application-specific information. Prefer resources over web search when possible.
- **Parameters**: {"type":"object","properties":{"cursor":{"type":"string","description":"Opaque cursor from a previous list_mcp_resources call; omit for the first page."},"server":{"type":"string","description":"MCP server name. Omit to list resources from every configured server."}},"additionalProperties":false}

### 3. list_mcp_resource_templates (function)
- **Sanitized Name**: `list_mcp_resource_templates`
- **Map Key**: `list_mcp_resource_templates`
- **Original Type**: `function`
- **Original Name**: `list_mcp_resource_templates`
- **Description**: Lists resource templates provided by MCP servers. Parameterized resource templates allow servers to share data that takes parameters and provides context to language models, such as files, database schemas, or application-specific information. Prefer resource templates over web search when possible.
- **Parameters**: {"type":"object","properties":{"cursor":{"type":"string","description":"Opaque cursor from a previous list_mcp_resource_templates call; omit for the first page."},"server":{"type":"string","description":"MCP server name. Omit to list resource templates from every configured server."}},"additionalProperties":false}

### 4. read_mcp_resource (function)
- **Sanitized Name**: `read_mcp_resource`
- **Map Key**: `read_mcp_resource`
- **Original Type**: `function`
- **Original Name**: `read_mcp_resource`
- **Description**: Read a specific resource from an MCP server given the server name and resource URI.
- **Parameters**: {"type":"object","properties":{"server":{"type":"string","description":"MCP server name exactly as configured. Must match the 'server' field returned by list_mcp_resources."},"uri":{"type":"string","description":"Resource URI to read. Must be one of the URIs returned by list_mcp_resources."}},"required":["server","uri"],"additionalProperties":false}

### 5. update_plan (function)
- **Sanitized Name**: `update_plan`
- **Map Key**: `update_plan`
- **Original Type**: `function`
- **Original Name**: `update_plan`
- **Description**: Updates the task plan.
Provide an optional explanation and a list of plan items, each with a step and status.
At most one step can be in_progress at a time.

- **Parameters**: {"type":"object","properties":{"explanation":{"type":"string","description":"Optional explanation for this plan update."},"plan":{"type":"array","description":"The list of steps","items":{"type":"object","properties":{"status":{"type":"string","description":"Step status.","enum":["pending","in_progress","completed"]},"step":{"type":"string","description":"Task step text."}},"required":["step","status"],"additionalProperties":false}}},"required":["plan"],"additionalProperties":false}

### 6. request_user_input (function)
- **Sanitized Name**: `request_user_input`
- **Map Key**: `request_user_input`
- **Original Type**: `function`
- **Original Name**: `request_user_input`
- **Description**: Request user input for one to three short questions and wait for the response. Set autoResolutionMs, from 60000 to 240000 milliseconds, only when the question is useful but non-blocking and continuing with best judgment is acceptable if the user does not answer; omit it when explicit user input is required. This tool is only available in Plan mode.
- **Parameters**: {"type":"object","properties":{"autoResolutionMs":{"type":"number","description":"Optional auto-resolution window in milliseconds, from 60000 to 240000. Include this only when the question is useful but non-blocking and continuing with best judgment is acceptable if the user does not answer; omit it when explicit user input is required before continuing. Use 60000 for lightly helpful context and up to 240000 when the answer would materially unblock better work."},"questions":{"type":"array","description":"Questions to show the user. Prefer 1 and do not exceed 3","items":{"type":"object","properties":{"header":{"type":"string","description":"Short header label shown in the UI (12 or fewer chars)."},"id":{"type":"string","description":"Stable identifier for mapping answers (snake_case)."},"options":{"type":"array","description":"Provide 2-3 mutually exclusive choices. Put the recommended option first and suffix its label with \"(Recommended)\". Do not include an \"Other\" option in this list; the client will add a free-form \"Other\" option automatically.","items":{"type":"object","properties":{"description":{"type":"string","description":"One short sentence explaining impact/tradeoff if selected."},"label":{"type":"string","description":"User-facing label (1-5 words)."}},"required":["label","description"],"additionalProperties":false}},"question":{"type":"string","description":"Single-sentence prompt shown to the user."}},"required":["id","header","question","options"],"additionalProperties":false}}},"required":["questions"],"additionalProperties":false}

### 7. list_available_plugins_to_install (function)
- **Sanitized Name**: `list_available_plugins_to_install`
- **Map Key**: `list_available_plugins_to_install`
- **Original Type**: `function`
- **Original Name**: `list_available_plugins_to_install`
- **Description**: # List plugin/connector install candidates

Use this tool only when both are true:
- The user explicitly asks to use a specific plugin or connector that is not already available in the current context or active `tools` list.
- `tool_search` is not available, or it has already been called and did not find or make the requested tool callable.

Returns known plugins and connectors that can be passed to `request_plugin_install`. When both a plugin and a connector match, prefer the plugin; use the connector only when its corresponding plugin is already installed.

- **Parameters**: {"type":"object","properties":{},"required":[],"additionalProperties":false}

### 8. request_plugin_install (function)
- **Sanitized Name**: `request_plugin_install`
- **Map Key**: `request_plugin_install`
- **Original Type**: `function`
- **Original Name**: `request_plugin_install`
- **Description**: # Request plugin/connector install

Use this tool only after `list_available_plugins_to_install` returns a plugin or connector that exactly matches the user's explicit request.

Do not use it for adjacent capabilities, broad recommendations, or tools that merely seem useful. Pass the returned `tool_type` through directly, and pass the returned `id` as `tool_id`.

IMPORTANT: DO NOT call this tool in parallel with other tools.
- **Parameters**: {"type":"object","properties":{"action_type":{"type":"string","description":"Suggested action for the tool. Use \"install\"."},"suggest_reason":{"type":"string","description":"Concise one-line user-facing reason why this plugin or connector can help with the current request."},"tool_id":{"type":"string","description":"Connector or plugin id to suggest."},"tool_type":{"type":"string","description":"Type of discoverable tool to suggest. Use \"connector\" or \"plugin\"."}},"required":["tool_type","action_type","tool_id","suggest_reason"],"additionalProperties":false}

### 9. apply_patch (custom)
- **Sanitized Name**: `custom_tool_apply_patch`
- **Description**: Use the `apply_patch` tool to edit files. This is a FREEFORM tool, so do not wrap the patch in JSON.

### 10. view_image (function)
- **Sanitized Name**: `view_image`
- **Map Key**: `view_image`
- **Original Type**: `function`
- **Original Name**: `view_image`
- **Description**: View a local image file from the filesystem when visual inspection is needed. Use this for images already available on disk.
- **Parameters**: {"type":"object","properties":{"detail":{"type":"string","description":"Image detail level. Defaults to `high`; use `original` to preserve exact resolution.","enum":["high","original"]},"path":{"type":"string","description":"Local filesystem path to an image file."}},"required":["path"],"additionalProperties":false}

### 11. image_gen (namespace)
- **Sanitized Name**: `image_gen`
- **Namespace**: `image_gen`
- **Sub-tools**: 1

  - Sub-tool: **imagegen**
    - **Map Key**: `image_gen__imagegen`
    - **Original Name**: `imagegen`
    - **Description**: The `image_gen.imagegen` tool enables image generation from descriptions and editing of existing images based on specific instructions. Use it when:

- The user requests an image based on a scene description, such as a diagram, portrait, comic, meme, or any other visual.
- The user wants to modify an attached or previously generated image with specific changes, including adding or removing elements, altering colors, improving quality/resolution, or transforming the style (e.g., cartoon, oil painting).

Guidelines:
- In code mode, pass the result to `generatedImage(result)`.
- Omit both `referenced_image_paths` and `num_last_images_to_include` when generating a brand new image.
- For edits, use `referenced_image_paths` when every target image has a local file path.
- If you have not seen a local image yet, use `view_image` to inspect it before editing.
- Use `num_last_images_to_include` only when at least one target image has no local file path.
- Set `num_last_images_to_include` to the smallest number of recent conversation images that includes every target image, up to 5.
- Never provide both `referenced_image_paths` and `num_last_images_to_include`.
- If neither mechanism can include every target image, ask the user to attach the missing images again.
- Directly generate the image without reconfirmation or clarification unless required images must be attached again.
- After each image generation, do not mention anything related to download. Do not summarize the image. Do not ask followup question. Do not say ANYTHING after you generate an image.
- Always use this tool for image editing unless the user explicitly requests otherwise. Do not use the `python` tool for image editing unless specifically instructed.

    - **Parameters**: {"type":"object","properties":{"num_last_images_to_include":{"type":["integer","null"]},"prompt":{"type":"string"},"referenced_image_paths":{"type":["array","null"],"items":{"type":"string","description":"A path that is guaranteed to be absolute and normalized (though it is not guaranteed to be canonicalized or exist on the filesystem).\n\nIMPORTANT: When deserializing an `AbsolutePathBuf`, a base path must be set using [AbsolutePathBufGuard::new]. If no base path is set, the deserialization will fail unless the path being deserialized is already absolute."}}},"required":["prompt"],"additionalProperties":false}
### 12.  (tool_search)
- **Sanitized Name**: `tool`
- **Description**: # Tool discovery

Searches over deferred tool metadata with BM25 and exposes matching tools for the next model call.

You have access to tools from the following sources:
- Canva: Create, review, edit designs
- Codex Document Control: Use Codex Document Control to find connected document sessions, inspect the tools supported by a selected session, and execute one supported tool against that session. Call `list_document_sessions` first to choose the intended connected session, call `get_document_tool_schemas` before constructing tool arguments, then call `execute_document_command` with a caller-stable `idempotency_key`. Use this only for connected Codex document control; do not use it for general spreadsheet, presentation, or document tasks without a connected document session.
- Figma: Turn code into editable design
- GitHub: Access repositories, issues, and pull requests. Required for some features such as Codex
- Hotline: Look up local hotline information for the user based on country inferred from the conversation. You must use this tool before providing helpline information; do not guess.
- Hugging Face: Provides access to search and inspect Hugging Face models, datasets, Spaces, and research papers, including detailed metadata and repository READMEs. Also enables searching and retrieving Hugging Face and Gradio documentation, and managing remote CPU/GPU jobs (create, list, monitor, and run code in containers) on the Hugging Face hub.
- OpenAI Platform: Use OpenAI Platform when the user wants to create, set up, copy, download, or use an OpenAI API key, including OPENAI_API_KEY or sk-proj keys. Also use it when code, commands, docs, or environment setup in the conversation requires an OpenAI API key, even if the user did not explicitly ask to create one. Do not generate key setup instructions inline when this app can be used. In normal ChatGPT chat surfaces, open the secure API key setup flow. In Codex, follow the installed Codex API key setup skill and use create_encrypted_openai_api_key only from a trusted local-write flow.
- Plugin Management: Use Plugin Management, not named plugin tools, for plugin permissions/removal. In Codex, use plugin_management.uninstall_plugin for Codex plugins; uninstall_app is only for ChatGPT plugins/connector links. Clarify ambiguous matches using exact IDs. Route what a named plugin may read/write/do or ask first, and named connector/app requests with approval labels (Always ask, Any changes, Important actions, Never ask, Use my default), even without plugin/permission wording. Call the matching permission or uninstall tool for a clear target/mode or global/default change. Ask without calling for missing/broad targets (Google), vague/conflicting modes, risky removal, or delegated choice. For disable, explain it is unsupported and make no call. Exclude install/connect/how-to/undo, OAuth/admin scopes, npm/Chrome/code plugins, and ordinary use. Complete all actions; report only confirmed results.
- Sites: Use Sites to build, save, deploy, and inspect websites such as landing pages, portfolios, dashboards, portals, trackers, hubs, games, and internal tools. Always use Sites when .openai/hosting.json exists. Use Sites skills for local implementation, validation, source preparation, and artifact packaging. Use this connector for site creation, runtime environment variables, versions, production deployments, and access controls. Read .openai/hosting.json before creating a site and reuse its project_id when present. Treat Sites IDs and cursors as opaque: copy them exactly from .openai/hosting.json or Sites responses as applicable, and never invent, reformat, derive, or substitute them. Never call create_site more than once for the same local site. Push the exact source state before saving a version. commit_sha must identify that pushed state, and any archive must be built from it. Deploy only saved versions; every Sites deployment URL is production. Inspect deployment status when the initial result is non-terminal or the user asks for progress. Unless the user asks for local-only work or a saved version without deployment, finish deployable site work with a production deployment.
- Supabase: Manage and query databases
- Vercel: Provides access to documentation search for a specific hosting and deployment platform and tools to manage its projects and deployments, including listing projects, deployments, build logs, and creating temporary authenticated share URLs or fetching protected deployment URLs. Also supports deploying the current project through the platform’s API, listing teams, and checking domain availability and pricing.
- node_repl: Use `js` to run JavaScript in the persistent Node-backed kernel. When a skill or prompt says to use `node_repl`, call this server's `js` execution tool. Calls default to a 30000 ms (30 seconds) timeout when `timeout_ms` is omitted. The runtime exposes `nodeRepl.cwd`, `nodeRepl.homeDir`, `nodeRepl.tmpDir`, `nodeRepl.requestMeta`, `nodeRepl.setResponseMeta(...)`, and `await nodeRepl.emitImage(...)`. Top-level bindings persist across `js` calls until `js_reset`; do not redeclare existing `const` or `let` names. Reuse existing bindings, use top-level `var` for reusable state that may be assigned again, or choose a fresh descriptive name. Use `js_add_node_module_dir` before `js` when a skill provides an extra package directory, and use dynamic imports like `await import("playwright")` rather than filesystem paths under `./node_modules`.

Use Cases:
- Control the Chrome browser in conjunction with the Chrome Plugin. Prefer this method of controlling Chrome over alternatives (such as Computer Use) unless the user explicitly mentions an alternative.
- openai-api-key-local-confirmation: Use confirm_openai_api_key_local_destination after the OpenAI Platform picker returns a key name and target ids. It asks the developer to confirm or edit the local env-file destination before a secret is created or written.
- sites-design-picker: Use choose_site_design only after the Sites skill generates exactly three comparable one-shot design-option previews. It may be called sequentially for up to four distinct design decisions; wait for each selection before preparing the next picker.
Some of the tools may not have been provided to you upfront, and you should use this tool (`tool_search`) to search for the required tools. For MCP tool discovery, always use `tool_search` instead of `list_mcp_resources` or `list_mcp_resource_templates`.
- **Parameters**: {"type":"object","properties":{"limit":{"type":"number","description":"Maximum number of tools to return. Defaults to 8."},"query":{"type":"string","description":"Search query for deferred tools."}},"required":["query"],"additionalProperties":false}

## Name Conflicts

None found.

## Duplicate Original Names

None found.

