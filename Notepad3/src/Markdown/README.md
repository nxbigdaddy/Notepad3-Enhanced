# Markdown preview

Native host integration: View > Markdown Preview, Ctrl+Shift+F12.
Startup demonstration: `Notepad3.exe /n /mdpreview document.md`.
Runtime initialization and generated-page navigation succeeded in a real launch after
allowing host-issued `data:text/html` navigations. Screenshot-based layout acceptance
and editing/image interaction checks remain pending. Set `NP3_MARKDOWN_DIAGNOSTICS`
to a development log path to record status events without document contents.
The preview is created on demand and destroyed when toggled off. Editing notifications
are debounced for 350 ms. The first version uses an equal-width split and limits live
rendering to 2 MiB. The limit does not affect normal text editing.

Dependencies:

- MD4C 0.5.2: https://github.com/mity/md4c/tree/release-0.5.2
  Parser and HTML renderer sources are vendored unchanged, with MIT license.
- Microsoft.Web.WebView2 1.0.4258.31: obtained from NuGet's official flat-container
  endpoint. Headers, architecture-specific loaders, license, and notices are vendored.
  The installed Evergreen WebView2 Runtime is required only for preview.

Raw Markdown HTML is disabled. Preview scripts load only from the packaged asset
virtual host allowed by CSP. Document-authored scripts, host objects and host messaging
are disabled. Native preview supports Mermaid fenced blocks and MD4C math nodes.
The generated page uses a restrictive CSP, a virtual host for document-relative images,
and cancels navigation away from its generated page. External images are not loaded.
There is no bridge that lets document content invoke application commands.

Pending acceptance: real panel rendering, local images, real-time editing, repeated
toggle/resize, runtime-missing behavior, long paths, dark mode and DPI. Native window
automation encountered capture timeout and window identity errors in the first run.
Parser regression tests and native host compilation do not establish these behaviors.

Later improvements: draggable split, preview scroll preservation, syntax coloring of
code blocks, scroll synchronization, Mermaid/math and export. Tabs require a separate
document-state refactor.

## Advanced preview (iteration 29)

The render-assets directory must accompany the executable. Scripts/styles/fonts use
np3-assets.invalid mapped only to this application resource folder; document-relative
images remain on the separate np3-preview.invalid host. Missing assets show basic
Markdown and an explicit source-text fallback notice. Live preview pauses at 2 MiB
source or 1 MiB generated UTF-8 HTML so NavigateToString remains within its 2 MiB
parameter size limit. No inline script or user HTML is enabled.

Ready polling is bound to the current render version. Scroll restoration follows
formula/diagram/font layout. Hidden controllers receive bounded frame fallback;
controller visibility is explicitly enabled. Capture writes a new PNG only and
checks version/window lifetime, result status and complete byte count.

Tests: scripts/test-markdown.cmd (33 checks), scripts/test-markdown-export.cmd
(21 baseline export checks), scripts/test-workspace.ps1 -Executable
Notepad3/Bin/Release_x64_v143/Notepad3.exe -Preview (116 checks), and
python scripts/test-ai-panel.py --preview (130 combined checks).
Actual native preview trace/PNG: artifacts/workspace-test-1225f06fe4d9430c919fc1be0053d3b6.
Preview supports graph/math now; HTML/PDF advanced rendering integration is pending.
