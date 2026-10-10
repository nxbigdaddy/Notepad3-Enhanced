/* Notepad3 trusted renderer. Markdown source stays text until parsed by pinned libraries. */
(() => {
  'use strict';
  const state = window.np3RenderState = {done: false, diagrams: 0, formulas: 0, errors: 0};
  const failure = (element, message) => {
    state.errors++;
    const note = document.createElement('p');
    note.className = 'np3-render-error'; note.textContent = message;
    element.insertAdjacentElement('afterend', note);
  };
  const finish = async () => {
    try {
      if (document.fonts) await document.fonts.ready;
      await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    } finally { state.done = true; }
  };
  const run = async () => {
    try {
      const equations = [...document.querySelectorAll('x-equation')];
      for (let i = 0; i < equations.length; i++) {
        const node = equations[i], source = node.textContent;
        if (i >= 512 || source.length > 16384 || !window.katex) { failure(node, 'Formula exceeds rendering limits or renderer is unavailable.'); continue; }
        try {
          window.katex.render(source, node, {displayMode: node.getAttribute('type') === 'display', trust: false, throwOnError: true, strict: 'warn', maxExpand: 1000, maxSize: 10});
          state.formulas++;
        } catch (_) { node.textContent = source; failure(node, 'Formula could not be rendered; the source is retained.'); }
      }
      const diagrams = [...document.querySelectorAll('pre > code.language-mermaid')];
      if (window.mermaid) window.mermaid.initialize({startOnLoad: false, securityLevel: 'strict', htmlLabels: false, flowchart: {htmlLabels: false}, suppressErrorRendering: true, maxTextSize: 65536, maxEdges: 1024, secure: ['secure', 'securityLevel', 'startOnLoad', 'maxTextSize', 'maxEdges', 'suppressErrorRendering', 'htmlLabels', 'flowchart', 'themeCSS']});
      for (let i = 0; i < diagrams.length; i++) {
        const node = diagrams[i], source = node.textContent;
        if (i >= 64 || source.length > 65536 || !window.mermaid) { failure(node.parentElement, 'Diagram exceeds rendering limits or renderer is unavailable.'); continue; }
        try {
          const result = await window.mermaid.render('np3-diagram-' + i, source);
          const parsed = new DOMParser().parseFromString(result.svg, 'image/svg+xml');
          const svg = parsed.documentElement;
          if (svg.localName !== 'svg' || parsed.querySelector('parsererror')) throw new Error('Invalid SVG');
          svg.querySelectorAll('script, foreignObject, iframe, image').forEach(item => item.remove());
          for (const element of [svg, ...svg.querySelectorAll('*')]) {
            for (const attr of [...element.attributes]) {
              if (/^on/i.test(attr.name) || ((attr.localName === 'href') && !attr.value.startsWith('#'))) element.removeAttributeNode(attr);
            }
          }
          const container = document.createElement('div'); container.className = 'np3-diagram';
          container.appendChild(document.importNode(svg, true)); node.parentElement.replaceWith(container); state.diagrams++;
        } catch (_) { failure(node.parentElement, 'Diagram could not be rendered; the source is retained.'); }
      }
    } catch (_) { state.errors++; }
    finally { await finish(); }
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run, {once: true}); else run();
})();
