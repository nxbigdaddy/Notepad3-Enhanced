# AI 模块

## 第二十五轮：协议与凭据基础

当前已接入 AiCore 构建和原生模块测试，**尚无 AI 界面及 HTTP 请求执行**。没有连接真实服务或生成假模型结果；后续继续接入可配置服务、后台执行、取消、差异确认及版本安全应用。

### 协议

请求按 Chat Completions 文本协议构造 model、stream:false 和 system/user 消息。默认操作包括解释、翻译、改写、生成，另可传入补充说明。默认解释与翻译使用中文；改写保留原语言和含义，生成按用户说明输出。模型名由配置提供，不硬编码供应商或模型。输入按 UTF-8 验证，控制字符和引号/反斜线做 JSON 转义。

请求输入上限 256 KiB、模型名 256 字节、补充说明 4096 字节。响应上限 2 MiB，先严格验证完整 JSON，再读取首个 choices 的 message.content，解码转义和 Unicode 代理对。拒绝空文本、null、非法 UTF-8、孤立代理、NUL 和重复的目标字段；如果服务提供 finish_reason，则只接受 stop，其他终止原因标记不完整，不能视为可应用的完整结果。兼容只返回内容而不提供终止原因的服务。

协议核对使用 OpenAI Docs 的[官方创建 Chat Completion 文档](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)。这里是文本兼容协议实现，不宣称已验证任一远程供应商或模型。

### 凭据与地址

密钥通过 Windows Credential Manager 的 Generic Credential 保存到 Notepad3/AI/<profile>，不写应用 INI。profile 限制长度和字符；密钥限制 2048 字节及可打印非空白 ASCII，拒绝换行头注入。读取结果由调用方拥有，AiCredential_Free 清零并释放。保存、读取、更新及删除返回实际 Windows 状态。

服务完整 URL 接受 HTTPS；本地 HTTP 只接受 localhost、127.0.0.1 或 IPv6 回环，不允许远程明文 HTTP、URL 用户名/密码、片段或控制字符。TLS 连接、重定向规则及具体取消机制尚待 HTTP 层实现。

## 验证

scripts/test-ai-core.cmd 的 47 项检查通过：地址边界、四操作合法 JSON、中文字节与转义、输入限额、首个候选、代理对解码、截断/空/工具调用拒绝、重复字段、非法 JSON/Unicode、Windows 凭据不存在/保存/独立读取/更新/非法写入保持原键/删除及再次读取确认。凭据测试只创建独立 PID/时间戳 profile 的临时假密钥，结束时删除，不读取已有用户服务配置。

完整 Release x64 构建及新版 99 项工作区检查通过。两种编码保存字节及 HTML 内容独立核对继续通过。证据 artifacts/workspace-test-04cff0f26d3b411b8e6673dfc8050206；包 artifacts/ai-core-x64-v25/Notepad3.exe，模块测试 artifacts/ai_core_test.exe。
