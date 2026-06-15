# 国际化 (i18n)

[English](../i18n.md) | **简体中文**

Vultra 在引擎核心中内置了一个小型本地化子系统（`I18nSystem`，通过
`II18nService` 暴露）。它服务于引擎、编辑器以及基于引擎构建的游戏。编辑器 UI 已
完全本地化；游戏可以注册并覆盖自己的目录。

内置的编辑器语言：**英语**（`en`，作为基准来源与回退）、**简体中文**
（`zh-CN`）、**日语**（`ja`）和**韩语**（`ko`）。

## 目录

目录采用 JSON 格式，以嵌套形式编写，并在加载时扁平化为点分键
（`{"menu":{"file":{"save":"Save"}}}` 变为键 `menu.file.save`）。叶子值为字符串；一个
保留的顶层 `"@meta": { "name": "..." }` 对象设置该 locale 的显示名称，且不会被扁平化。
文件以其 BCP-47 标签命名：`en.json`、`zh-CN.json`、`ja.json`、`ko.json`。

```json
{
  "@meta": { "name": "日本語" },
  "common": { "save": "保存", "cancel": "キャンセル" },
  "menu":   { "file": { "saveScene": "シーンを保存" } }
}
```

每个 locale 都必须拥有与 `en.json` **相同的键集**（英语是任何缺失项的回退）。
编辑器目录位于 `builtin/i18n/`，并与 `en.json` 保持精确的键一致性。

### 嵌入式分发

`builtin/i18n/*.json` 中的编辑器目录由 `vultra.builtin_pack` 规则（连同着色器、字体、
纹理和渲染图一起）打包进构建期的 zstd `builtin.vpk` —— 该规则会
glob `builtin/i18n/`，因此新增的文件会被自动拾取，无需任何代码生成步骤。在
启动时，编辑器通过 `vultra::builtin::read("i18n/<locale>.json", ...)`
直接从包中读取原始 JSON，并用
`registerCatalog(locale, json, "editor", 100)` 注册它（参见
`source/vultra_app/src/editor_app/editor_i18n.cpp`）。其中不涉及 lz4 头部嵌入。
（`registerCompressedCatalog()` 在服务上仍然存在，供发布 lz4 数据块的调用者使用，但
编辑器不再使用它。）一个在磁盘上发布目录的游戏只需读取文件并以原始 JSON 文本调用
`registerCatalog()`。

## 在代码中使用翻译

包含 `vultra/core/i18n/i18n.hpp` 并调用环境自由函数（它们会通过进程级活动的
translator 解析，因此深层 UI 绘制代码不需要 `EngineContext`）：

```cpp
ImGui::TextUnformatted(vultra::tr("inspector.transform.position")); // display text
ImGui::Text("%s", vultra::trf("entity.uuid", uuid).c_str());        // fmt format string + args
```

- `tr(key)` 返回一个帧稳定的 `const char*`（在下一次 `setLanguage` 之前有效）；缺失时返回
  键本身，因此未翻译的键是可见的，并被 `missingKeys()` 记录。
- `trf(key, args...)` 翻译一个作为 `fmt` 格式字符串的值，然后对其进行格式化。**占位符
  （`{}`、`{:.2f}`、……）必须在每个 locale 中完全一致**，否则 `fmt::format` 会抛出异常 —— 目录
  构建会验证这一点。

### ImGui id 稳定性

ImGui 从控件的可见标签（直到 `###`）派生其 id。随语言变化的标签会破坏停靠
（`imgui.ini`）、打开/关闭状态和弹出窗口匹配。对于任何同时也是 id 的翻译标签
（窗口、按钮、菜单项、标签页、弹出窗口），请使用 `trId`：

```cpp
ImGui::Begin(vultra::trId("window.inspector.title", "InspectorWindow")); // 検査###InspectorWindow
if (ImGui::Button(vultra::trId("common.save", "btnSave"))) { ... }
```

可见文本跟随语言变化；`###StableId` 使控件 id 保持与语言无关。

## 切换和检测语言

- `setLanguage(locale)` 实时切换；用 `onLanguageChanged` 注册的监听器会刷新。
  `availableLanguages()` / `displayName(locale)` 驱动编辑器设置的语言下拉菜单。
- 首次启动时，编辑器会检测操作系统语言（`detectSystemLocale()` →
  `resolveStartupLocale()`）：简体中文 → `zh-CN`，日语 → `ja`，韩语 → `ko`，否则
  `en`。该选择会持久化到编辑器设置中，并在后续启动时被遵循。

## 目录堆叠（引擎 + 编辑器 + 游戏）

每个注册的目录都携带一个 `domain` 和一个 `int priority`；活动映射是当前 locale 下
所有目录按优先级升序的合并（更高者胜出）。约定：**引擎 0，编辑器
100，游戏 200**。游戏通过为同一点分键注册自己的值来覆盖任何引擎/编辑器字符串，
同时仍然继承它未覆盖的所有内容。

```cpp
auto& i18n = ctx().services.require<vultra::II18nService>();
i18n.registerCatalog("en", myGameEnglishJson, "game", 200);
i18n.registerCatalog("ja", myGameJapaneseJson, "game", 200);
```

## 添加一种语言

1. 将 `builtin/i18n/en.json` 复制为 `builtin/i18n/<tag>.json`，将 `@meta.name` 设为该语言的
   本族名称，并翻译每个值（保持键结构和所有格式占位符与 `en.json` 完全一致）。
2. 将新标签添加到 `registerBuiltinEditorCatalogs()` 中硬编码的 locale 列表（`{"en",
   "zh-CN", "ja", "ko"}`）；由于 `builtin_pack` 会 glob `builtin/i18n/`，文件本身会从包中
   自动拾取。如有需要，在 `resolveStartupLocale()` 中添加操作系统 locale 检测。
3. 确保内置字体覆盖新的文字系统。编辑器合并了 **Noto Sans CJK** 的一个子集
   （SIL OFL），它已经覆盖了中文、日文和韩文；新的文字系统（例如西里尔文、泰文）
   可能需要在字体构建中扩展字体子集的保留集。

## 字体与 HiDPI

CJK 字形从一个内置的 Noto Sans CJK 子集
（`builtin/fonts/noto_sans_cjk.otf`）折叠进默认的 ImGui 字体，由 ImGui 1.92 的动态
图集按需光栅化 —— 切换语言时无需字形范围或图集重建。

UI 缩放是用户的 **Application Scale** 设置（编辑器设置 → 外观）。它通过
`ScaleAllSizes` 缩放 ImGui 样式，并通过 `ui::dp()`
（`vultra/function/imgui/imgui_dpi.hpp`）缩放每一个硬编码的像素字面量；将设计像素常量
包裹在 `ui::dp(...)` 中，使其跟踪该设置。桌面操作系统 DPI 系数固定为 1.0（Android 会
自动应用其显示密度）。
