# AGENTS.md

## 沟通
- 用中文回答；代码、命令、标识符、commit message 用英文。
- 简洁直接，无客套；commit、PR、代码中不用 emoji。
- 用户提问时先回答，再改代码。
- 回应反馈先表态同意/不同意，再说改了什么。
- 解释复杂设计：问题、具体例子、方案及其必要性。

## 工作方式
- 非平凡任务先给简短计划；需求不清先问。
- 只改任务相关代码，不做无关重构；优先用项目已有工具和依赖。
- 大范围修改或审计前完整读相关文件，不只依赖搜索片段。
- 外部 API/库先查类型定义或官方文档，不要猜。
- 删除看似有意为之的功能或代码前先问。
- 完成 = 代码改完 + 相关 lint/typecheck/test 已运行并通过；未运行须说明原因。

## 子 agent（主动使用）
你是 lead：主线程负责需求、决策、整合和最终输出，把噪音大或可并行的工作交给子 agent。工具不支持子 agent 时忽略本节。

应当委派：
- 只读的大范围探索（找实现、梳理调用链、跨目录搜索），用只读类型（explore/explorer/scout）。
- 输出量大的工作（跑测试套件、分析日志、读长文档），只返回结论和失败项。
- 2 个以上独立、无共享状态的任务：每个问题域一个子 agent，同一轮并行派发。
- 改完代码后派只读 reviewer 检查正确性、安全、回归和缺失测试。

不要委派：
- 需频繁来回沟通或多阶段共享大量上下文的工作。
- 快速局部小改动；对延迟敏感的场景。
- 相互关联的问题（修一个可能修好其他），或子 agent 会互相干扰时。
- 并行写同一批文件；确需并行写时用独立 worktree。

派发 prompt：
- 聚焦一个明确问题域。
- 自包含：子 agent 看不到对话历史，给全上下文、文件路径和约束（如"不要改其他代码"）。
- 写明返回内容（摘要、`file:line` 引用、按严重程度分级）及是否需等全部完成再继续。

返回后：读每份摘要，核对改动无冲突，跑完整检查再整合；不盲信子 agent 结论。

## GitNexus（代码知识图谱）
仓库已建索引（有 `.gitnexus/`）时适用；项目 AGENTS.md 中由 `gitnexus analyze` 生成的 GitNexus 段落优先于本节。
- 查调用方、依赖、执行流程先用图谱，文本搜索只用于结果为空、`UNKNOWN` 或字面量：概念/流程用 `query`，具名符号用 `context`。
- 改函数、类、方法前先跑 `impact`（direction upstream），报告调用方、受影响流程和风险；HIGH/CRITICAL 风险须明确提醒。
- `risk: UNKNOWN` 或调用方为空不代表安全或未被使用（可能是动态调用、跨语言调用），删改前先用文本搜索确认。
- commit 前跑 `detect_changes`（scope all）；结果 `partial` 或 `truncated` 不算通过，须重跑。
- 重命名用 `rename`：先 `dry_run: true` 预览，确认后再应用，不用查找替换。
- 结果显示索引落后或分叉（behind/diverged）时，先 `gitnexus analyze` 重建索引。
- 无 MCP 时用 CLI：`npx gitnexus impact <symbol> --direction upstream`、`npx gitnexus query "<concept>"`。

## 简单优先，不过度防御
- 只做被要求或明确必要的改动；修 bug 不顺手清理周边；简单功能不加额外可配置性。
- 用最少代码解决问题：不加没要求的功能，不为一次性代码建 helper/抽象，不为假想需求设计。200 行能缩到 50 行就重写。
- 不为不可能发生的情况加 try/catch、fallback、校验；信任内部代码和类型/框架保证，只在系统边界（用户输入、外部 API、网络/文件 I/O）校验；不对类型已保证的值做运行时检查。
- 除非用户要求，不加向后兼容 shim、旧接口别名、"以防万一"的分支。
- 不给未改动的代码加注释、docstring、类型注解；只在逻辑不自明处写注释。
- 每行改动都应能追溯到用户请求；只清理自己造成的未使用 import/变量。
- 自问"资深工程师会觉得过于复杂吗？"，是就简化。

## 测试：少而有效
- 覆盖行为和关键路径，不追求数量；一个测试只测一个行为，名字含 "and" 就拆。
- 不写重复琐碎的测试（getter/setter、框架本身、类型已保证的内容），不穷举参数组合。
- 用真实代码，非必要不 mock；断言真实行为，不断言 mock 本身。
- 修 bug 先写复现测试再修；重构前后测试都要通过。
- 实现须对所有合法输入正确，不为通过测试硬编码或特判。
- 只跑与改动相关的测试；全量套件收尾时跑一次。

## 代码质量
- 遵循仓库已有风格，与周围代码一致。
- 不为修类型错误删除或降级代码；必要时升级依赖。

## Git
- Commit message 用 Conventional Commits（`feat:` `fix:` `chore:` 等）。
- 未经明确要求不 commit、不 push、不改写历史（`reset --hard`、`push --force`、rebase 已推送提交）。

## 边界
- 总是：改动后跑相关检查；说明改了哪些文件。
- 先问：新增生产依赖；修改 CI、部署、数据库迁移、生产配置；大范围删除文件。
- 绝不：提交或打印 secrets、API key、`.env`；修改 vendor 或生成文件（改源头再重新生成）；未确认就执行破坏性命令。

## 持续改进
- 同类错误被纠正时，提议把规则补进最近一级的 AGENTS.md（全局偏好写本文件，项目约定写仓库 AGENTS.md）。
- 重复出现的流程做成 skill（`~/.agents/skills/<name>/SKILL.md`），不塞进本文件。

<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **pourover_v1_lvgl_macos** (108294 symbols, 180629 relationships, 452 execution flows).

> Index stale? Run `node .gitnexus/run.cjs analyze --index-only` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? Bootstrap with `npx`, `bunx`, or `pnpm dlx` — e.g. `bunx gitnexus@latest analyze` (npm 11 npx crash; #1939).

## Always Do

- **MUST run impact before editing.** Use `impact({target: "symbolName", direction: "upstream"})` or `node .gitnexus/run.cjs impact "symbolName" --direction upstream --repo .`; report callers, processes, and risk. Never substitute grep for graph analysis.
- **MUST analyze graph changes before committing.** Use `detect_changes({scope: "all"})` (MCP) or `node .gitnexus/run.cjs detect-changes --scope all --repo .` (CLI fallback). `partial: true` or `truncated: true` is not a clean check — a zero means unseen, not unaffected; re-run it. For regression review: `detect_changes({scope: "compare", base_ref: "master"})` or `node .gitnexus/run.cjs detect-changes --scope compare --base-ref "master" --repo .`.
- MUST warn on HIGH/CRITICAL `risk` pre-edit; never use `riskSharedAxes` to waive a HIGH/CRITICAL `risk` warning. Compare File/symbol: MCP File omits axes; Graph-RAG expands File.
- **MUST treat `risk: UNKNOWN` as unresolved, not as low.** An empty caller set is not evidence the symbol is unused — it can also mean the callers are not resolvable by the index (plain-object property access, dynamic dispatch, cross-language calls). `impact` pairs `UNKNOWN` with a `riskNote` saying so. Confirm with a text search before treating the symbol as safe to change or delete; do not proceed on the strength of a zero.
- **MUST use `query({search_query: "concept"})` for concepts/flows, `context({name: "symbolName"})` for a named symbol, or `impact` for blast radius, on read-only callers, dependencies, imports, or execution flow.** Graph first; text search only for empty/`UNKNOWN`/literals.
- For security review, `explain({target: "fileOrSymbol"})` lists taint findings (source→sink flows; needs `analyze --pdg`).

## Never Do

- NEVER edit a function, class, or method before MCP/CLI impact analysis.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis, and never read `UNKNOWN` as an all-clear — it means the walk could not answer, which is the one verdict that requires confirming by other means.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit before MCP/CLI graph change analysis.

## Resources

| Resource | Use for |
| --- | --- |
| `gitnexus://repo/pourover_v1_lvgl_macos/context` | Codebase overview, check index freshness |
| `gitnexus://repo/pourover_v1_lvgl_macos/clusters` | All functional areas |
| `gitnexus://repo/pourover_v1_lvgl_macos/processes` | All execution flows |
| `gitnexus://repo/pourover_v1_lvgl_macos/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
| --- | --- |
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->
