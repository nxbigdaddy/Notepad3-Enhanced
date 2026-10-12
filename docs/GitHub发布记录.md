# GitHub 发布记录

2026-10-13：用户确认公开仓库 Notepad3-Enhanced，完成 GitHub CLI 浏览器授权；账号 nxbigdaddy。未使用聊天中提供的密码。

仓库：https://github.com/nxbigdaddy/Notepad3-Enhanced
首个开发预览：https://github.com/nxbigdaddy/Notepad3-Enhanced/releases/tag/v0.1.0-preview.134
标签对应提交：9cc5308（依赖许可打包增量，编辑器仍为v133）。

四个附件：Notepad3-Enhanced-portable-x64-v134.zip、Notepad3-extension-patch-x64-v134.zip、Notepad3-Enhanced-update-x64-v134.exe、SHA256SUMS-v134.txt。远端四个附件大小与GitHub SHA256 digest均与本地一致核对通过。GitHub会转换中文附件名，因此更新入口使用英文名。

本机 GitHub CLI：artifacts/github-cli/bin/gh.exe，授权存Windows凭据库。当前Clash Verge本地代理127.0.0.1:7897，仅在发布命令进程设置HTTP_PROXY/HTTPS_PROXY；不写全局代理、不记录令牌。后续如代理端口变更需重新核对。

整体功能尚未全部完成，发布标记prerelease。发布说明保留真实供应商调用、人工界面、真实安装等限制。后续维护可继续源码迭代和必要验证，每次公开发布前核对许可、私密资料排除和版本结果；不得将本地模拟测试写成真实服务验收。