# Notepad3 轻量扩展开发

基于上游 Notepad3，目标为 AI、编辑增强、专用文本工具、界面优化及 Markdown 预览。

当前状态见 [迭代进展](docs/迭代进展.md)，完整方案见 [开发方案](docs/Notepad3扩展开发方案.md)。

当前可构建开发版已有 Markdown 预览入口，窗口实际显示和交互验收仍未完成。

构建：`scripts/build.ps1 -SkipVersion`。测试：`scripts/test-markdown.cmd`。
打包：`scripts/package.ps1`。开发版输出：`artifacts/preview-x64/Notepad3.exe`。

源项目许可证和依赖版权声明保留在源码及开发包中。
