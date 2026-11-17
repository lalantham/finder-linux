# Project Conventions & Architect/Builder Workflow
- Keep all code edits minimal and surgical. Do not refactor unrelated code, reformat untouched lines, or add unnecessary dependencies.
- Before implementing any new feature or multi-file change, delegate planning to the `Plan` subagent (`subagent_type: "Plan"`, `run_in_background: false`) so `commandcode/claude-sonnet-5-5` designs the blueprint first.
- Once the plan returns, implement each step cleanly yourself, run verification tests via `bash`, and fix any errors before finishing.
