# Notepad3 Enhanced（非官方增强版）

基于 [Rizonesoft/Notepad3](https://github.com/rizonesoft/Notepad3) 源码开发，保持 Windows 原生编辑器风格，扩展 Markdown 预览、多标签、文件搜索、打开历史和 AI 辅助功能。本项目由独立维护者维护，不代表上游官方发布或背书。

当前扩展编译进主程序，更新包替换主程序及配套资源，不是原版可独立加载的插件。升级与恢复方法见 [补丁升级说明](docs/补丁升级.md)。

版本变更见 [更新日志](CHANGELOG.md)，下载见 [GitHub Releases](https://github.com/nxbigdaddy/Notepad3-Enhanced/releases)。当前状态见 [迭代进展](docs/迭代进展.md)，完整方案见 [开发方案](docs/Notepad3扩展开发方案.md)。

最新开发增量 v133 包含本地 Markdown 大纲、停笔自动刷新及标题定位；原生程序与本地模拟服务联合检查 484 项通过。测试证据和限制见迭代进展；模拟 AI 服务不代表真实供应商验收。百度网盘同步、连续流式对话及其他规划仍未全部实现。

构建：`scripts/build.ps1 -SkipVersion`。测试：`scripts/test-markdown.cmd`。
打包：`scripts/package.ps1 -PackageName markdown-x64`。构建输出：`Notepad3/Bin/Release_x64_v143/Notepad3.exe`；打包输出：`artifacts/markdown-x64/Notepad3.exe`。需要 Windows、Visual Studio 2022 C++ 工具链及适用 Windows SDK。

Notepad3 原代码使用 BSD 3-Clause，完整上游声明见 [许可证](Notepad3/Build/Docs/License.txt)。第三方依赖分别遵循其自身许可证（包括 MPL/GPL/MIT 等），保留各组件许可和版权声明，不能将整个依赖集合视为单一 BSD 许可。

AI Key 使用 Windows Credential Manager 保存。发布源码和产物时不得包含个人密钥、授权令牌、会话草稿、私人文档或本机测试输出。额外联网功能应由用户明确配置和发起。
