# Offline Markdown renderer assets

Pinned npm distributions: Mermaid 12.1.0 and KaTeX 0.19.0. Download origins, archive SHA-512 integrity and per-file SHA-256/byte counts are recorded in manifest.json. LICENSE-mermaid.txt and LICENSE-katex.txt accompany the upstream distributions. The Mermaid single-file browser bundle has no dynamic import dependencies; KaTeX CSS uses the shipped WOFF2 fonts. Older WOFF/TTF fallbacks in upstream CSS are not shipped because the supported Chromium/WebView2 runtime supports WOFF2.

Refresh only with the explicit pinned versions and integrity strings in scripts/fetch-markdown-render-assets.py. It verifies archives before writing selected regular files and checks destination containment. np3-render.js is Notepad3-owned source and is not a downloaded package file.

np3-render.js renders only code.language-mermaid and x-equation nodes, with strict Mermaid security and KaTeX trust:false. SVG is parsed as XML; executable elements, foreignObject/image and event/external href attributes are removed before adopting nodes. Invalid math restores the original text after KaTeX failure. Diagram limits: 64 diagrams, 65536 characters per diagram, 1024 edges. Formula limits: 512 formulas, 16384 characters per formula, 1000 macro expansions and maxSize 10. window.np3RenderState.done waits for fonts and two animation frames and will be the native PDF readiness signal.

The asset runner is verified independently but is not yet enabled in the native Markdown preview or exporters. Current native rendering remains script-disabled until the integration supplies a restricted asset virtual host and readiness handling.

Validation: python scripts/test-markdown-assets.py. Playwright with installed Edge, fallback reason Browser plugin not available. The runner starts an isolated loopback server, verifies 25 downloaded files, checks 3 SVG diagrams and 2 formulas, invalid graph/formula source preservation, attempted loosened security and script-link inertness, no page exceptions, and desktop/narrow screenshots. Evidence artifacts/markdown-assets-test-1791665829050567500. Only console warning is the upstream CSS deprecation of -ms-high-contrast-adjust; it does not affect these renders.

Primary configuration references: https://mermaid.js.org/config/schema-docs/config-properties-securitylevel.html and https://katex.org/docs/options.
