# `devbench/` — devbench Integration

[devbench](https://github.com/ArthurHub/devbench) is a separate F4SE plugin that runs a local MCP +
REST server inside the game, so an AI agent or a script can inspect and drive it. Other plugins add
their own tools to it through a small C ABI. This folder is the framework's side of that ABI.

> Part of the [F4VR Common Framework](../README.md) source tree.

## Vendored client API

| File | Contents |
|------|----------|
| [`DevBenchAPI.h`](DevBenchAPI.h) | The cross-plugin interface: `IDevBenchInterface001` (register tools, emit events), the handler types, and `GetDevBenchInterface001()`. |
| [`DevBenchAPI.cpp`](DevBenchAPI.cpp) | `GetDevBenchInterface001()`: the F4SE messaging handshake, with a `GetProcAddress` fallback on `devbench.dll`. |
| [`DevBenchAPI.LICENSE.txt`](DevBenchAPI.LICENSE.txt) | MIT license for these two files only; devbench itself is GPL-3.0. |

They are devbench's `include/` files, copied byte for byte from devbench **1.22.0**
([ArthurHub/devbench](https://github.com/ArthurHub/devbench) `cdf903e`). They stay unmodified
so that an update is a plain copy that can be diffed against upstream:

- Don't edit or reformat them. `.pre-commit-config.yaml` excludes them from every hook for that reason.
- To update, copy all three files from devbench's `include/` over these and note the new version
  and commit here.

The ABI is append-only: a vtable slot added in a later devbench exists only on hosts at least that
new, so check `GetBuildNumber()` before calling one. Every slot says which build introduced it.
