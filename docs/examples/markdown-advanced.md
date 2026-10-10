# Markdown 图表与公式

点击工具栏 **M** 打开预览，拖动中间分隔条调整预览宽度。

## 流程图

```mermaid
flowchart TD
    A[编写 Markdown] --> B[点击 M 预览]
    B --> C{内容正确？}
    C -->|是| D[保存文档]
    C -->|否| A
```

## 时序图

```mermaid
sequenceDiagram
    participant 作者
    participant 编辑器
    作者->>编辑器: 输入文字、图表和公式
    编辑器-->>作者: 更新右侧预览
```

## 数学公式

行内公式：$E=mc^2$。

展示公式：

$$
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
$$

| 功能 | 用法 |
|---|---|
| 图表 | `mermaid` 代码块 |
| 行内公式 | 用一个美元符号包围公式 |
| 展示公式 | 用两个美元符号包围公式 |
