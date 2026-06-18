# Codex 文档维护规范

## 文档拆分原则

- 一个文档只描述一类问题：规范、模块索引、运行流程、路线图不要混在同一个文件里。
- 面向 Codex 的文件应尽量短，方便模型检索和局部修改。
- `docs/modules` 只存放与仓库 `/modules` 目录对应的模块说明。
- `docs/common` 只存放与仓库 `/common` 目录对应的公共库说明。
- 新需求、路线图、阶段计划、实验方案放入 `docs/projects`。
- Codex 工作规范、自动化维护规则、会话摘要放入 `docs/codex`。
- 每次较大代码修改后的对话摘要、改动摘要，可单独放入 `docs/codex/sessions/`。

## 推荐目录

```text
docs/
├── common/
├── modules/
├── projects/
└── codex/
    ├── documentation_maintenance.md
    └── sessions/
```

## 目录职责

### `docs/modules`

用于帮助 Codex 快速理解 `/modules` 下的业务模块。文档应按模块边界组织，只描述已有模块的职责、接口、数据流、关键参数和常见修改点。

推荐命名：

```text
docs/modules/rune_detector.md
docs/modules/rune_fan.md
docs/modules/rune_group.md
```

不应放入：

- 新功能路线图。
- 长期项目计划。
- 训练实验记录。
- 与 `/modules` 无直接对应关系的设计草案。

### `docs/common`

用于帮助 Codex 快速理解 `/common` 下的公共基础库。文档应按公共能力组织，例如数学工具、轮廓封装、相机参数、调试工具、数据 IO 等。

推荐命名：

```text
docs/common/contour_proc.md
docs/common/math.md
docs/common/camera.md
```

不应放入：

- 上层业务模块说明。
- 新功能路线图。
- 临时实验记录。

### `docs/projects`

用于存放未来一段时间内的项目计划、新需求实现大纲和阶段性路线图。此目录中的文档可以描述尚未落地的模块、训练工程、实验方案和验收指标。

推荐命名：

```text
docs/projects/rune_keypoint_nn_project_outline.md
```

当项目落地并形成稳定代码后，应将稳定模块说明拆分沉淀到 `docs/modules` 或 `docs/common`。

### `docs/codex`

用于存放 Codex 自动化维护规范、文档维护规范和较大任务的会话摘要。这里的内容服务于后续自动化协作，不直接描述业务模块细节。

## 每次 Codex 改动后建议记录

```text
日期：YYYY-MM-DD
主题：一句话说明本次任务
涉及文件：列出关键文件
核心改动：3~8 条
遗留问题：可选
```

## 自动放置规则

Codex 后续新增文档时，应先判断文档类型：

```text
描述 /modules 下已有模块       -> docs/modules
描述 /common 下已有公共能力     -> docs/common
描述未来项目、路线图、新需求     -> docs/projects
描述 Codex 维护规范或会话摘要    -> docs/codex
```

如果一个文档同时包含模块说明和路线图，应拆成两个文件，避免让快速阅读文档变成混合记录。

新增 Markdown 文档后，应确认 `.gitignore` 没有继续忽略该文件。
