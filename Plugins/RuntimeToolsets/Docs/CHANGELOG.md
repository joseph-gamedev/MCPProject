# Changelog

## 0.1.0 — 2026-10-20

Phase 1.

- **Core:**
  - Plain-JSON API (`ListToolsets`, `DescribeToolset`, `Invoke`, `ExecuteNested`, `OnEvent`).
  - Game-thread dispatcher with an MPSC queue, a frame budget, async tools, timeouts and cancellation.
  - Schema builder and validator.
  - Per-session command history with transactions, checkpoints and irreversible markers.
  - Per-world scene graph and target resolver.
  - Log capture, settings and a usage report.
- **Toolsets, all at 1.0.0:**
  - session (8 tools)
  - actors (7)
  - scene (17)
  - reflection (7)
  - console (5)
  - log (3)
  - capture (3)
  - history (8)
- **Tests:** 64 automation tests, including schema snapshots, plus the `RuntimeToolsets.RunTests` runner for packaged builds.
- **Optional `RuntimeToolsetsMCP` adapter:**
  - Exposes the tools through Epic's ModelContextProtocol server on `127.0.0.1:8018/mcp`.
  - `Meta` exposure mode (default) or `Eager`.
