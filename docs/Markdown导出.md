# Markdown 导出

文件菜单新增 Export Markdown HTML，所有 26 个语言菜单已接入。导出读取当前缓冲区，包括尚未保存的修改；不会把它另存为编辑文档，不修改路径、编码、修改状态或撤销历史。任何工作区中已打开的文档路径都不可作为导出目标，避免导出 HTML 覆盖源文件。

## HTML 输出

输出为 UTF-8 HTML，无 BOM，采用已有 MD4C GFM 渲染，包括标题、表格、任务列表和代码块；原始 HTML 按文本显示。输出不加载脚本、框架或联网资源，CSP 默认拒绝资源，允许内联样式及 file/data 图片。

来源为命名文件时，HTML 基址设置为其目录的 file URI；未命名文档使用当前工作目录。Unicode、空格、#、& 等路径字符按 UTF-8 百分号编码，支持盘符和 UNC 目录。相对图片地址保持原文并通过该基址解析，因此导出文件可放在不同目录，但仍依赖原来源目录中的图片。当前不是携带全部资源的便携打包文件；浏览器实际显示本地图片和跨目录视觉效果仍待验收。

加入打印样式：浅色背景、18 mm 页边距、代码换行及表格/代码块的分页约束。HTML 本身的浏览器打印交互仍未验收；PDF 导出已单独接入，见下节。

输入限 8 MiB UTF-8，输出限 32 MiB，超限返回失败且不截断。内部非 UTF-8 代码页先严格转换为 Unicode，再转 UTF-8；非法字节报错。先在目标目录创建独立临时文件，完整写入并刷新后原子替换，目标锁定、无效数据或超限时保留已有输出。临时文件为导出 HTML 明文，与受保护的会话文件不同。

## 验证

`scripts/test-markdown-export.cmd` 通过 21 项检查：中文与代码/表格内容、原始 HTML 无执行、CSP、来源路径编码、UNC、打印样式、真实 UTF-8 导出字节、原子替换失败保留输出、非法 UTF-8、输入/输出限额及错误参数。证据 C:/Users/Administrator/AppData/Local/Temp/np3-export-中文 & #-22960-9510734，sample.html 保留用于查看。

实际编辑器工作区检查增至 50 项，其中新增当前未保存中文标题导出、版本/修改/撤销保持、源文件覆盖保护及导出后撤销；脚本独立读取 buffer-export.html 核对中文标题和不依赖预览虚拟域名，同时仍核对原始编辑文件的保存字节。证据 artifacts/workspace-test-9cafe51f613d4387afca3d57dcfd5c5e。Markdown 原有 26 项检查通过，完整 x64 Release 构建通过。

HTML 开发包 artifacts/markdown-export-x64-v13/Notepad3.exe。HTML 保存对话框、本地图片、浏览器显示和浏览器打印交互验收尚未完成；Mermaid、公式仍未实现。

## PDF 输出

文件菜单新增 Export Markdown PDF，所有 26 个语言资源接入。当前缓冲区先复制为独立 HTML 快照，再由隐藏 WebView2 生成 PDF。异步任务完成后验证 PDF 签名并原子替换目标；导出期间继续输入不会进入已有快照，目标锁定时保留此前文件。一个窗口同一时间允许一个 PDF 任务，重复请求返回忙，最长 60 秒。关闭隐藏任务窗口后弱引用回调不会重新激活文档。

输出使用 A4、18 mm 页边距、浅色打印样式、代码背景和本地图片；关闭脚本和开发工具，仅允许自己的快照页面导航。源目录通过只读虚拟域名映射加载图片，生成 PDF 后图片已嵌入。私有临时目录存储 HTML，成功/失败时清理。PDF 临时文件创建在目标目录，写入完成后替换，输入与 HTML 输出沿用 8 MiB / 32 MiB 限额；暂不序列化后台任务至会话。

使用本地快照文件导航，避免 NavigateToString 的 2 MiB 限制，依据 [Microsoft 本地内容说明](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content)。生成使用 [ICoreWebView2_7::PrintToPdf](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2_7?view=webview2-1.0.3650.58)，需要支持该接口的 WebView2 Runtime；运行时缺失或不支持时返回错误并保留已有输出。[页面设置单位为英寸](https://learn.microsoft.com/ja-jp/microsoft-edge/webview2/reference/win32/icorewebview2printsettings?view=webview2-1.0.2470-prerelease)。

`scripts/test-pdf-export.ps1` 的 12 项实际编辑器检查通过：源文件覆盖保护、独立快照任务、并发拒绝、真实 PDF 成功、导出期间编辑与撤销、已有 PDF 的锁定替换失败。脚本核对失败后 PDF SHA-256 与成功基线完全相同。`scripts/verify-pdf-export.py` 使用 pypdf 独立确认中文/英文、表格/代码、图片尺寸、A4、排除快照后的输入，再使用 Poppler 渲染；已查看页面 PNG，标题、正文、表格、代码背景和图片正常显示，没有截断或乱码。

最终证据 artifacts/pdf-test-b4bf7d48341841f287efa6b8983d1fb0，包含 export.pdf、baseline.pdf、extracted-text.txt、verification.json 与 page-1.png。完整 x64 Release 构建及 50 项工作区检查通过；工作区证据 artifacts/workspace-test-e0536e96d022404ebcb961c5dc5a5405。新版 artifacts/markdown-pdf-x64-v14/Notepad3.exe。

当前视觉核对覆盖一页固定样例，不代表长文分页、缺失运行时、关闭时中断、多个实例同时导出及真实保存对话框都已验收。这些边界继续保留在总验收清单中。
