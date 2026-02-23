#pragma once

const char* INDEX_HTML = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NMCL — Net Minecraft Launcher</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=Press+Start+2P&family=JetBrains+Mono:wght@300;400;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --lime:     #a3e635;
            --lime-bg:  rgba(163,230,53,0.08);
            --lime-bdr: rgba(163,230,53,0.28);
            --rust:     #ea580c;
            --amber:    #f59e0b;
            --dark:     #0a0d09;
            --panel:    #0f1410;
            --mid:      #182014;
            --sidebar:  #0c0f0b;
            --stroke:   #243020;
            --muted:    #4a6040;
            --text:     #b8c8a0;
            --sb-w:     56px;
            --sb-exp:   168px;
            --font-px:  'Press Start 2P', monospace;
            --font-m:   'JetBrains Mono', monospace;
        }

        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

        body {
            font-family: var(--font-m);
            background: var(--dark);
            color: var(--text);
            height: 100vh;
            display: flex;
            overflow: hidden;
            user-select: none;
            cursor: none; /* 隐藏系统光标 */
        }

        /* ══════════════════════════════════════
           自定义光标（来自 landing page）
           方块点 + 滞后追踪环
        ══════════════════════════════════════ */
        .cursor {
            position: fixed;
            width: 6px; height: 6px;
            background: var(--lime);
            pointer-events: none;
            z-index: 99999;
            /* transform 由 JS 写入，不需要 transition，保持即时响应 */
        }

        .cursor-ring {
            position: fixed;
            width: 22px; height: 22px;
            border: 1px solid rgba(163,230,53,0.5);
            pointer-events: none;
            z-index: 99998;
            /* transform 由 rAF 循环写入，自带缓动 */
            transition: opacity 0.2s;
        }

        /* 扫描线 */
        .scanlines {
            position: fixed; inset: 0;
            background: repeating-linear-gradient(
                to bottom, transparent 0px, transparent 3px,
                rgba(0,0,0,0.09) 3px, rgba(0,0,0,0.09) 4px
            );
            pointer-events: none;
            z-index: 9000;
        }

        /* ══ SIDEBAR ══ */
        .sidebar {
            width: var(--sb-w);
            background: var(--sidebar);
            border-right: 1px solid var(--stroke);
            display: flex;
            flex-direction: column;
            align-items: flex-start;
            position: relative;
            z-index: 200;
            flex-shrink: 0;
            overflow: hidden;
            transition: width 0.28s cubic-bezier(0.4,0,0.2,1);
        }

        .sidebar.expanded { width: var(--sb-exp); }

        .sb-brand {
            width: 100%; height: 48px;
            display: flex; align-items: center;
            border-bottom: 1px solid var(--stroke);
            padding: 0 0 0 18px; gap: 10px; flex-shrink: 0;
        }

        .sb-brand-icon {
            font-family: var(--font-px);
            font-size: 0.42rem; color: var(--lime);
            line-height: 1.5; flex-shrink: 0;
        }

        .sb-brand-label {
            font-family: var(--font-m);
            font-size: 0.58rem; color: var(--muted);
            letter-spacing: 0.1em; white-space: nowrap;
            opacity: 0; transition: opacity 0.2s 0.1s;
        }

        .sidebar.expanded .sb-brand-label { opacity: 1; }

        .nav-list {
            width: 100%; display: flex;
            flex-direction: column; padding: 10px 0; flex: 1;
        }

        .nav-item {
            width: 100%; height: 44px;
            display: flex; align-items: center;
            padding: 0 0 0 16px; gap: 12px;
            color: var(--muted);
            border-left: 2px solid transparent;
            transition: color 0.15s, border-color 0.15s, background 0.15s;
            flex-shrink: 0;
        }

        .nav-item:hover  { color: var(--text); background: var(--mid); }
        .nav-item.active { color: var(--lime); border-left-color: var(--lime); background: var(--lime-bg); }
        .nav-item svg    { width: 18px; height: 18px; fill: currentColor; flex-shrink: 0; }

        .nav-label {
            font-family: var(--font-m);
            font-size: 0.62rem; letter-spacing: 0.1em;
            white-space: nowrap; opacity: 0;
            transform: translateX(-6px);
            transition: opacity 0.2s 0.08s, transform 0.2s 0.08s;
        }

        .sidebar.expanded .nav-label { opacity: 1; transform: none; }

        .sb-ver {
            width: 100%; padding: 6px 0 8px 17px;
            font-family: var(--font-px); font-size: 0.3rem;
            color: var(--stroke); line-height: 1.8; flex-shrink: 0;
        }

        .sb-toggle {
            width: 100%; height: 40px;
            display: flex; align-items: center;
            padding: 0 0 0 17px; gap: 11px;
            color: var(--stroke);
            border-top: 1px solid var(--stroke);
            transition: color 0.15s, background 0.15s; flex-shrink: 0;
        }

        .sb-toggle:hover { color: var(--muted); background: var(--mid); }

        .sb-toggle-icon {
            width: 18px; height: 18px; flex-shrink: 0;
            transition: transform 0.28s cubic-bezier(0.4,0,0.2,1);
        }

        .sidebar.expanded .sb-toggle-icon { transform: rotate(180deg); }

        .sb-toggle-label {
            font-family: var(--font-m); font-size: 0.55rem;
            letter-spacing: 0.1em; white-space: nowrap;
            opacity: 0; transition: opacity 0.2s 0.08s;
        }

        .sidebar.expanded .sb-toggle-label { opacity: 1; }

        /* ══ MAIN CANVAS ══ */
        .main-content {
            flex: 1; position: relative; overflow: hidden;
            background: radial-gradient(ellipse at 70% 30%, rgba(24,32,20,0.55) 0%, transparent 60%), var(--dark);
        }

        .main-content::before {
            content: ''; position: absolute; inset: 0;
            background: repeating-linear-gradient(
                60deg, var(--stroke) 0, var(--stroke) 1px, transparent 1px, transparent 22px
            );
            opacity: 0.18; pointer-events: none;
        }

        .watermark {
            position: absolute; top: 50%; left: 50%;
            transform: translate(-50%, -50%);
            font-family: var(--font-px);
            font-size: clamp(1.6rem, 4.5vw, 3rem);
            color: var(--stroke); text-align: center;
            line-height: 1.8; pointer-events: none; opacity: 0.4;
        }

        .watermark-sub {
            display: block; font-family: var(--font-m);
            font-size: clamp(0.38rem, 0.9vw, 0.6rem);
            color: var(--muted); opacity: 0.5;
            letter-spacing: 0.28em; margin-top: 0.5rem;
        }

        /* ══════════════════════════════════════════
           FLOAT PANEL — position:fixed
           坐标系 = 整个视口，可以自由拖到任何位置
        ══════════════════════════════════════════ */
        .float-panel {
            position: fixed;           /* ← 关键：相对视口定位 */
            background: var(--panel);
            border: 1px solid var(--stroke);
            display: none;
            flex-direction: column;
            z-index: 500;
            box-shadow: 0 20px 56px rgba(0,0,0,0.6);
            will-change: transform;
            overflow: hidden;
            min-width: 220px;
            min-height: 80px;
        }

        .float-panel.visible { display: flex; }
        .float-panel.on-top  { z-index: 600; }

        /* 顶角发光边 */
        .float-panel::before {
            content: ''; position: absolute; inset: -1px;
            background: linear-gradient(
                135deg, rgba(163,230,53,0.12) 0%, transparent 35%,
                transparent 65%, rgba(234,88,12,0.07) 100%
            );
            pointer-events: none; z-index: -1;
        }

        /* 全屏/最小化过渡，仅在这两个操作时挂上 */
        .float-panel.animating {
            transition:
                left   0.26s cubic-bezier(0.4,0,0.2,1),
                top    0.26s cubic-bezier(0.4,0,0.2,1),
                width  0.26s cubic-bezier(0.4,0,0.2,1),
                height 0.26s cubic-bezier(0.4,0,0.2,1);
        }

        @keyframes panelIn {
            from { opacity:0; transform: translateY(10px) scale(0.97); }
            to   { opacity:1; transform: none; }
        }

        .float-panel.minimized .panel-body,
        .float-panel.minimized .resize-handle { display: none !important; }
        .float-panel.minimized { height: auto !important; min-height: 0; }

        /* ── 标题栏 ── */
        .panel-bar {
            height: 38px; padding: 0 10px 0 12px;
            border-bottom: 1px solid var(--stroke);
            background: var(--mid);
            display: flex; align-items: center; gap: 7px;
            flex-shrink: 0; z-index: 2;
        }

        /* ── 交通灯 ── */
        .tl-wrap { display: flex; align-items: center; gap: 6px; flex-shrink: 0; }

        .tl {
            width: 11px; height: 11px; border-radius: 50%;
            flex-shrink: 0; position: relative;
            transition: filter 0.12s, transform 0.1s;
        }

        .tl:hover  { filter: brightness(1.3); transform: scale(1.15); }
        .tl:active { transform: scale(0.88); }
        .tl.red    { background: #ef4444; box-shadow: 0 0 5px rgba(239,68,68,0.45); }
        .tl.yellow { background: #f59e0b; box-shadow: 0 0 5px rgba(245,158,11,0.45); }
        .tl.green  { background: var(--lime); box-shadow: 0 0 5px rgba(163,230,53,0.45); }

        .tl::after {
            content: ''; position: absolute; inset: 0;
            display: flex; align-items: center; justify-content: center;
            font-size: 7px; font-weight: 900;
            color: rgba(0,0,0,0.5); opacity: 0;
            transition: opacity 0.12s;
            line-height: 11px; text-align: center;
        }

        .tl-wrap:hover .tl.red::after    { opacity:1; content:'✕'; }
        .tl-wrap:hover .tl.yellow::after { opacity:1; content:'−'; }
        .tl-wrap:hover .tl.green::after  { opacity:1; content:'⤢'; font-size:6px; }

        .panel-title {
            font-family: var(--font-m); font-size: 0.57rem;
            color: var(--muted); letter-spacing: 0.1em;
            flex: 1; white-space: nowrap;
            overflow: hidden; text-overflow: ellipsis;
        }

        .panel-body { padding: 1.4rem; overflow-y: auto; flex: 1; min-height: 0; }

        /* ══ RESIZE HANDLES ══ */
        .resize-handle { position: absolute; z-index: 10; }
        .resize-handle.n  { top:0;    left:8px;  right:8px;  height:5px;  cursor:n-resize; }
        .resize-handle.s  { bottom:0; left:8px;  right:8px;  height:5px;  cursor:s-resize; }
        .resize-handle.e  { right:0;  top:8px;   bottom:8px; width:5px;   cursor:e-resize; }
        .resize-handle.w  { left:0;   top:8px;   bottom:8px; width:5px;   cursor:w-resize; }
        .resize-handle.ne { top:0;    right:0;   width:12px; height:12px; cursor:ne-resize; }
        .resize-handle.nw { top:0;    left:0;    width:12px; height:12px; cursor:nw-resize; }
        .resize-handle.se { bottom:0; right:0;   width:12px; height:12px; cursor:se-resize; }
        .resize-handle.sw { bottom:0; left:0;    width:12px; height:12px; cursor:sw-resize; }

        .resize-handle.se::after,.resize-handle.sw::after,
        .resize-handle.ne::after,.resize-handle.nw::after {
            content:''; position:absolute; width:5px; height:5px;
            border-color:var(--muted); border-style:solid; opacity:0.35;
        }
        .resize-handle.se::after { bottom:2px; right:2px; border-width:0 1px 1px 0; }
        .resize-handle.sw::after { bottom:2px; left:2px;  border-width:0 0 1px 1px; }
        .resize-handle.ne::after { top:2px;    right:2px; border-width:1px 1px 0 0; }
        .resize-handle.nw::after { top:2px;    left:2px;  border-width:1px 0 0 1px; }

        /* ══ FORM ══ */
        .form-group { margin-bottom: 1.3rem; }

        .flabel {
            display: flex; align-items: center; gap: 0.4rem;
            font-family: var(--font-m); font-size: 0.56rem;
            color: var(--muted); margin-bottom: 0.6rem;
            letter-spacing: 0.15em; text-transform: uppercase;
        }
        .flabel::before { content:'//'; color:var(--stroke); }

        /* ── 自定义版本下拉 ── */
        .px-select { position: relative; width: 100%; }

        .px-select-box {
            width: 100%; background: var(--dark);
            border: 1px solid var(--stroke);
            padding: 0.6rem 2.4rem 0.6rem 0.9rem;
            font-family: var(--font-m); font-size: 0.75rem;
            color: var(--text); letter-spacing: 0.05em;
            display: flex; align-items: center;
            position: relative; min-height: 38px;
            transition: border-color 0.15s;
        }

        .px-select-box::after {
            content: ''; position: absolute;
            right: 11px; top: 50%;
            transform: translateY(-50%);
            width: 0; height: 0;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 6px solid var(--muted);
            transition: border-color 0.15s, transform 0.18s;
        }

        .px-select.open .px-select-box {
            border-color: var(--lime-bdr);
        }

        .px-select.open .px-select-box::after {
            border-top-color: var(--lime);
            transform: translateY(-50%) rotate(180deg);
        }

        .px-select-val { flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }

        .px-dropdown {
            position: absolute;
            top: calc(100% + 2px); left: 0; right: 0;
            background: var(--mid);
            border: 1px solid var(--lime-bdr);
            max-height: 180px; overflow-y: auto;
            z-index: 9999; display: none;
        }

        .px-dropdown::-webkit-scrollbar { width: 3px; }
        .px-dropdown::-webkit-scrollbar-track { background: var(--dark); }
        .px-dropdown::-webkit-scrollbar-thumb { background: var(--stroke); }

        .px-select.open .px-dropdown { display: block; }

        .px-option {
            padding: 0.55rem 0.9rem;
            font-family: var(--font-m); font-size: 0.73rem;
            color: var(--muted); letter-spacing: 0.04em;
            display: flex; align-items: center; gap: 0.6rem;
            border-bottom: 1px solid var(--stroke);
            transition: background 0.1s, color 0.1s;
        }

        .px-option:last-child { border-bottom: none; }

        .px-option::before {
            content: ''; width: 6px; height: 6px;
            background: var(--stroke); flex-shrink: 0;
            transition: background 0.1s;
        }

        .px-option:hover { background: rgba(163,230,53,0.06); color: var(--text); }
        .px-option:hover::before { background: var(--muted); }

        .px-option.selected { color: var(--lime); background: var(--lime-bg); }
        .px-option.selected::before { background: var(--lime); }

        .px-opt-tag {
            margin-left: auto;
            font-family: var(--font-px); font-size: 0.3rem;
            color: var(--stroke); border: 1px solid var(--stroke);
            padding: 0.1rem 0.3rem; letter-spacing: 0.05em; flex-shrink: 0;
        }

        .px-opt-tag.release  { border-color: rgba(163,230,53,0.3); color: rgba(163,230,53,0.6); }
        .px-opt-tag.snapshot { border-color: rgba(245,158,11,0.3); color: rgba(245,158,11,0.6); }

        .px-loading {
            padding: 0.8rem 0.9rem;
            font-family: var(--font-m); font-size: 0.65rem; color: var(--muted);
            display: flex; align-items: center; gap: 0.5rem;
        }

        .px-loading::before {
            content: ''; width: 6px; height: 6px;
            background: var(--lime);
            animation: px-blink 0.8s step-end infinite;
        }

        @keyframes px-blink { 50% { opacity:0; } }

        /* ── 文本输入框 ── */
        .px-input-wrap {
            position: relative; background: var(--dark);
            border: 1px solid var(--stroke);
            display: flex; align-items: center;
            transition: border-color 0.15s;
        }

        .px-input-wrap:focus-within { border-color: var(--lime-bdr); }

        .px-input-wrap::before {
            content: '▶'; font-size: 0.55rem; color: var(--stroke);
            padding: 0 0.5rem 0 0.8rem; flex-shrink: 0;
            transition: color 0.15s; pointer-events: none;
        }

        .px-input-wrap:focus-within::before { color: var(--lime); }

        .px-input {
            flex: 1; background: transparent; border: none; outline: none;
            color: var(--text); padding: 0.62rem 0.8rem 0.62rem 0;
            font-family: var(--font-m); font-size: 0.78rem;
            letter-spacing: 0.05em; caret-color: var(--lime);
        }

        /* ── 内存滑块 ── */
        .mem-row {
            display: flex; justify-content: space-between;
            align-items: center; margin-bottom: 0.7rem;
        }

        .mem-val {
            font-family: var(--font-px); font-size: 0.42rem;
            color: var(--lime); letter-spacing: 0.05em;
        }

        .px-slider-wrap {
            position: relative; width: 100%; height: 20px;
            display: flex; align-items: center;
        }

        .px-track { position: absolute; left:0; right:0; height:3px; background: var(--stroke); }
        .px-fill  { position: absolute; left:0; height:3px; background: var(--lime); pointer-events:none; }

        .px-ticks { position: absolute; left:0; right:0; top:12px; pointer-events:none; }

        .px-tick {
            position: absolute;
            font-family: var(--font-px); font-size: 0.22rem;
            color: var(--stroke); transform: translateX(-50%);
        }

        .px-range {
            position: absolute; left:0; right:0; width:100%;
            margin: 0; -webkit-appearance:none;
            background: transparent; border:none; outline:none;
            height: 20px; z-index: 2; opacity: 0;
        }

        .px-thumb {
            position: absolute;
            width: 10px; height: 16px;
            background: var(--lime); top:50%; transform:translateY(-50%);
            clip-path: polygon(0 0, calc(100% - 3px) 0, 100% 3px, 100% 100%, 3px 100%, 0 calc(100% - 3px));
            pointer-events:none; z-index:1;
            box-shadow: 0 0 6px rgba(163,230,53,0.4);
            transition: filter 0.1s;
        }

        .px-slider-wrap:hover .px-thumb { filter: brightness(1.2); }

        /* ── 启动按钮 ── */
        .px-launch-btn {
            width: 100%; padding: 0.9rem; border: none;
            background: var(--lime); color: var(--dark);
            font-family: var(--font-px); font-size: 0.5rem;
            letter-spacing: 0.08em;
            display: flex; align-items: center; justify-content: center; gap: 10px;
            clip-path: polygon(0 0, calc(100% - 8px) 0, 100% 8px, 100% 100%, 8px 100%, 0 calc(100% - 8px));
            transition: filter 0.12s, transform 0.08s;
            position: relative; overflow: hidden;
        }

        .px-launch-btn::after {
            content:''; position:absolute; inset:0;
            background: linear-gradient(135deg, rgba(255,255,255,0.1) 0%, transparent 50%);
        }

        .px-launch-btn::before {
            content:''; position:absolute;
            top:0; left:-100%; width:60%; height:100%;
            background: linear-gradient(90deg, transparent, rgba(255,255,255,0.15), transparent);
            transform: skewX(-15deg);
        }

        .px-launch-btn:hover:not(:disabled)::before { animation: px-scan 0.45s ease; }
        @keyframes px-scan { to { left:160%; } }

        .px-launch-btn:hover:not(:disabled) { filter:brightness(1.1); transform:translate(-1px,-1px); }
        .px-launch-btn:active:not(:disabled) { transform:none; }
        .px-launch-btn:disabled {
            background: var(--mid); color: var(--muted);
            clip-path:none; border:1px solid var(--stroke);
        }

        /* ── 状态行 ── */
        .status-msg {
            margin-top: 0.9rem; font-family: var(--font-m);
            font-size: 0.63rem; color: var(--muted);
            min-height: 18px; display:flex; align-items:center; gap:0.5rem;
        }

        .status-msg::before { content:'$'; color:var(--lime); flex-shrink:0; }

        /* 用户占位 */
        .user-placeholder { text-align:center; padding:2rem 1rem; }
        .user-placeholder-title { font-family:var(--font-px); font-size:0.55rem; color:var(--muted); margin-bottom:0.8rem; line-height:1.8; }
        .user-placeholder-sub   { font-family:var(--font-m); font-size:0.62rem; color:var(--stroke); letter-spacing:0.15em; }

        /* ══ JAVA MODAL ══ */
        .modal {
            display:none; position:fixed; inset:0;
            background:rgba(0,0,0,0.7); backdrop-filter:blur(5px);
            justify-content:center; align-items:center; z-index:9000;
        }

        .modal-content {
            background:var(--panel); border:1px solid var(--stroke);
            width:360px; position:relative;
        }

        .modal-content::before {
            content:''; position:absolute; inset:-1px;
            background:linear-gradient(135deg, rgba(234,88,12,0.18) 0%, transparent 40%);
            z-index:-1;
        }

        .modal-header {
            padding:0.5rem 1rem; border-bottom:1px solid var(--stroke);
            background:rgba(234,88,12,0.07);
            display:flex; align-items:center; gap:0.5rem;
        }

        .modal-header-dot { width:7px; height:7px; border-radius:50%; background:var(--rust); }
        .modal-title { font-family:var(--font-m); font-size:0.6rem; color:var(--rust); letter-spacing:0.1em; }
        .modal-body-wrap { padding:1.3rem; }
        .modal-body { font-family:var(--font-m); font-size:0.68rem; color:var(--muted); margin-bottom:1rem; line-height:2; }
        .modal-body b { color:var(--text); font-weight:400; }
        .modal-actions { display:flex; gap:10px; margin-top:1.1rem; }

        .modal-btn {
            flex:1; padding:0.65rem; border:none;
            font-family:var(--font-m); font-size:0.63rem;
            letter-spacing:0.08em;
            transition:filter 0.12s, background 0.12s;
        }

        .btn-primary {
            background:var(--lime); color:var(--dark); font-weight:700;
            clip-path:polygon(0 0, calc(100% - 5px) 0, 100% 5px, 100% 100%, 5px 100%, 0 calc(100% - 5px));
        }

        .btn-primary:hover { filter:brightness(1.08); }
        .btn-secondary { background:transparent; border:1px solid var(--stroke); color:var(--muted); }
        .btn-secondary:hover { border-color:var(--muted); color:var(--text); }

        .java-prog-row { display:flex; justify-content:space-between; margin-bottom:0.45rem; }
        #javaStatusText { font-family:var(--font-m); font-size:0.6rem; color:var(--muted); }
        #javaPercent    { font-family:var(--font-px); font-size:0.45rem; color:var(--lime); }
        .progress-bar   { height:3px; background:var(--stroke); overflow:hidden; }
        .progress-fill  { height:100%; background:var(--lime); width:0%; transition:width 0.3s; }

        ::-webkit-scrollbar { width:4px; }
        ::-webkit-scrollbar-track { background:var(--dark); }
        ::-webkit-scrollbar-thumb { background:var(--stroke); }
    </style>
</head>
<body>

<!-- 自定义光标 -->
<div class="cursor"      id="cursor"></div>
<div class="cursor-ring" id="cursorRing"></div>

<div class="scanlines"></div>

<!-- ════ SIDEBAR ════ -->
<div class="sidebar" id="sidebar">
    <div class="sb-brand">
        <span class="sb-brand-icon">NM<br>CL</span>
        <span class="sb-brand-label">Net MC 启动器</span>
    </div>

    <div class="nav-list">
        <div class="nav-item" id="nav-play"     onclick="togglePanel('play')"     title="启动游戏">
            <svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
            <span class="nav-label">启动</span>
        </div>
        <div class="nav-item" id="nav-settings" onclick="togglePanel('settings')" title="设置">
            <svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
            <span class="nav-label">设置</span>
        </div>
        <div class="nav-item" id="nav-user"     onclick="togglePanel('user')"     title="用户">
            <svg viewBox="0 0 24 24"><path d="M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z"/></svg>
            <span class="nav-label">用户</span>
        </div>
    </div>

    <div class="sb-ver">Alpha 0.1</div>

    <div class="sb-toggle" onclick="toggleSidebar()" title="展开/收起">
        <svg class="sb-toggle-icon" viewBox="0 0 24 24" fill="currentColor">
            <path d="M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z"/>
        </svg>
        <span class="sb-toggle-label">收起</span>
    </div>
</div>

<!-- ════ MAIN CANVAS ════ -->
<div class="main-content" id="mainContent">
    <div class="watermark">
        NMCL
        <span class="watermark-sub">// Net Minecraft Launcher — Alpha 0.1</span>
    </div>
</div>

<!-- ════ FLOAT PANELS (fixed, 视口坐标) ════ -->

<!-- 启动面板 -->
<div class="float-panel" id="panel-play" style="left:120px;top:80px;width:360px;">
    <div class="panel-bar">
        <div class="tl-wrap">
            <div class="tl red"    onclick="closePanel('play')"    title="关闭"></div>
            <div class="tl yellow" onclick="minimizePanel('play')" title="最小化/恢复"></div>
            <div class="tl green"  onclick="maximizePanel('play')" title="全屏/恢复"></div>
        </div>
        <span class="panel-title">nmcl — 启动游戏</span>
    </div>
    <div class="panel-body">
        <div class="form-group">
            <label class="flabel">游戏版本</label>
            <div class="px-select" id="versionPxSelect">
                <div class="px-select-box" onclick="toggleSelect('versionPxSelect')">
                    <span class="px-select-val" id="versionDisplay">正在加载...</span>
                </div>
                <div class="px-dropdown" id="versionDropdown">
                    <div class="px-loading">加载版本列表中</div>
                </div>
            </div>
            <select id="versionSelect" style="display:none;"></select>
        </div>

        <button class="px-launch-btn" id="launchBtn" onclick="launchGame()">
            <svg style="width:14px;height:14px;fill:currentColor" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
            启动游戏
        </button>
        <div id="statusMsg" class="status-msg">准备就绪</div>
    </div>
    <div class="resize-handle n"  data-dir="n"></div>
    <div class="resize-handle s"  data-dir="s"></div>
    <div class="resize-handle e"  data-dir="e"></div>
    <div class="resize-handle w"  data-dir="w"></div>
    <div class="resize-handle ne" data-dir="ne"></div>
    <div class="resize-handle nw" data-dir="nw"></div>
    <div class="resize-handle se" data-dir="se"></div>
    <div class="resize-handle sw" data-dir="sw"></div>
</div>

<!-- 设置面板 -->
<div class="float-panel" id="panel-settings" style="left:520px;top:80px;width:360px;">
    <div class="panel-bar">
        <div class="tl-wrap">
            <div class="tl red"    onclick="closePanel('settings')"></div>
            <div class="tl yellow" onclick="minimizePanel('settings')"></div>
            <div class="tl green"  onclick="maximizePanel('settings')"></div>
        </div>
        <span class="panel-title">nmcl — 设置</span>
    </div>
    <div class="panel-body">
        <div class="form-group">
            <label class="flabel">离线用户名</label>
            <div class="px-input-wrap">
                <input class="px-input" type="text" id="username" value="Steve" placeholder="输入用户名">
            </div>
        </div>
        <div class="form-group">
            <div class="mem-row">
                <label class="flabel" style="margin:0">内存分配</label>
                <span class="mem-val" id="memValue">2048 MB</span>
            </div>
            <div class="px-slider-wrap" id="memSliderWrap">
                <div class="px-track"></div>
                <div class="px-fill"  id="memFill"></div>
                <div class="px-thumb" id="memThumb"></div>
                <div class="px-ticks" id="memTicks"></div>
                <input class="px-range" type="range" id="memory" min="1024" max="8192" value="2048" step="512">
            </div>
        </div>
    </div>
    <div class="resize-handle n"  data-dir="n"></div>
    <div class="resize-handle s"  data-dir="s"></div>
    <div class="resize-handle e"  data-dir="e"></div>
    <div class="resize-handle w"  data-dir="w"></div>
    <div class="resize-handle ne" data-dir="ne"></div>
    <div class="resize-handle nw" data-dir="nw"></div>
    <div class="resize-handle se" data-dir="se"></div>
    <div class="resize-handle sw" data-dir="sw"></div>
</div>

<!-- 用户面板 -->
<div class="float-panel" id="panel-user" style="left:280px;top:260px;width:300px;">
    <div class="panel-bar">
        <div class="tl-wrap">
            <div class="tl red"    onclick="closePanel('user')"></div>
            <div class="tl yellow" onclick="minimizePanel('user')"></div>
            <div class="tl green"  onclick="maximizePanel('user')"></div>
        </div>
        <span class="panel-title">nmcl — 用户</span>
    </div>
    <div class="panel-body">
        <div class="user-placeholder">
            <div class="user-placeholder-title">// 用户资料</div>
            <div class="user-placeholder-sub">即将推出...</div>
        </div>
    </div>
    <div class="resize-handle n"  data-dir="n"></div>
    <div class="resize-handle s"  data-dir="s"></div>
    <div class="resize-handle e"  data-dir="e"></div>
    <div class="resize-handle w"  data-dir="w"></div>
    <div class="resize-handle ne" data-dir="ne"></div>
    <div class="resize-handle nw" data-dir="nw"></div>
    <div class="resize-handle se" data-dir="se"></div>
    <div class="resize-handle sw" data-dir="sw"></div>
</div>

<!-- ════ Java 弹窗 ════ -->
<div id="javaModal" class="modal">
    <div class="modal-content">
        <div class="modal-header">
            <div class="modal-header-dot"></div>
            <div class="modal-title">缺少 Java 运行环境</div>
        </div>
        <div class="modal-body-wrap">
            <div class="modal-body">
                此版本需要 <b>Java <span id="targetJavaVer">8</span></b>。<br>
                是否自动下载并安装？
            </div>
            <div id="javaProgressUI" style="display:none;">
                <div class="java-prog-row">
                    <span id="javaStatusText">正在下载...</span>
                    <span id="javaPercent">0%</span>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill" id="javaProgressBar"></div>
                </div>
            </div>
            <div class="modal-actions" id="javaModalActions">
                <button class="modal-btn btn-secondary" onclick="closeJavaModal()">取消</button>
                <button class="modal-btn btn-primary"   onclick="confirmInstallJava()">安装</button>
            </div>
        </div>
    </div>
</div>

<script>
"use strict";

// ══════════════════════════════════════════════
//  自定义光标 — 方块点 + 滞后追踪环
//  完全移植自 landing page index.html
// ══════════════════════════════════════════════
const cursor = document.getElementById('cursor');
const ring   = document.getElementById('cursorRing');
let mx = 0, my = 0, rx = 0, ry = 0;

document.addEventListener('mousemove', e => {
    mx = e.clientX; my = e.clientY;
    // 方块点：即时跟随，偏移半个点大小使中心对齐
    cursor.style.transform = `translate(${mx - 3}px,${my - 3}px)`;
}, { passive: true });

// 环：rAF 缓动追踪
(function loop() {
    rx += (mx - rx - 11) * 0.1;
    ry += (my - ry - 11) * 0.1;
    ring.style.transform = `translate(${rx}px,${ry}px)`;
    requestAnimationFrame(loop);
})();

// 悬停可点击元素时环变暗
function refreshCursorTargets() {
    document.querySelectorAll('button, .nav-item, .tl, .px-option, .px-select-box, .sb-toggle, .modal-btn, .panel-bar').forEach(el => {
        el.addEventListener('mouseenter', () => { ring.style.opacity = '0.15'; });
        el.addEventListener('mouseleave', () => { ring.style.opacity = '1'; });
    });
}

// ══════════════════════════════════════════════
//  版本选择器
// ══════════════════════════════════════════════
let selectedVersion = null;

function toggleSelect(id) {
    const el = document.getElementById(id);
    const wasOpen = el.classList.contains('open');
    document.querySelectorAll('.px-select.open').forEach(s => s.classList.remove('open'));
    if (!wasOpen) el.classList.add('open');
}

document.addEventListener('click', e => {
    if (!e.target.closest('.px-select')) {
        document.querySelectorAll('.px-select.open').forEach(s => s.classList.remove('open'));
    }
});

function buildVersionList(versions) {
    const dropdown    = document.getElementById('versionDropdown');
    const nativeSelect = document.getElementById('versionSelect');
    const display     = document.getElementById('versionDisplay');
    dropdown.innerHTML = '';
    nativeSelect.innerHTML = '';

    if (!versions || !versions.length) {
        dropdown.innerHTML = '<div class="px-loading">未找到任何版本</div>';
        return;
    }

    const releases  = versions.filter(v => v.type === 'release');
    const snapshots = versions.filter(v => v.type === 'snapshot').slice(0, 5);

    [...releases, ...snapshots].forEach((v, i) => {
        const opt = document.createElement('div');
        opt.className = 'px-option';
        opt.dataset.val = v.id;
        opt.textContent = v.id;

        const tag = document.createElement('span');
        tag.className = 'px-opt-tag ' + v.type;
        tag.textContent = v.type === 'release' ? '正式版' : '快照';
        opt.appendChild(tag);

        opt.addEventListener('click', () => {
            document.querySelectorAll('.px-option').forEach(o => o.classList.remove('selected'));
            opt.classList.add('selected');
            display.textContent = v.id;
            selectedVersion = v.id;
            nativeSelect.value = v.id;
            document.querySelectorAll('.px-select.open').forEach(s => s.classList.remove('open'));
        });

        dropdown.appendChild(opt);

        const nOpt = document.createElement('option');
        nOpt.value = v.id; nOpt.text = v.id;
        nativeSelect.appendChild(nOpt);

        if (i === 0) {
            opt.classList.add('selected');
            display.textContent = v.id;
            selectedVersion = v.id;
        }
    });

    refreshCursorTargets();
}

// ══════════════════════════════════════════════
//  内存滑块
// ══════════════════════════════════════════════
function initSlider() {
    const range = document.getElementById('memory');
    const fill  = document.getElementById('memFill');
    const thumb = document.getElementById('memThumb');
    const label = document.getElementById('memValue');
    const ticks = document.getElementById('memTicks');
    const min = parseInt(range.min), max = parseInt(range.max);

    [['1G',1024],['2G',2048],['4G',4096],['6G',6144],['8G',8192]].forEach(([lbl, v]) => {
        const s = document.createElement('span');
        s.className = 'px-tick';
        s.textContent = lbl;
        s.style.left = ((v - min) / (max - min) * 100) + '%';
        ticks.appendChild(s);
    });

    function update() {
        const v = parseInt(range.value);
        const pct = (v - min) / (max - min) * 100;
        fill.style.width  = pct + '%';
        thumb.style.left  = `calc(${pct}% - 5px)`;
        label.textContent = (v / 1024).toFixed(1).replace('.0','') + ' GB (' + v + ' MB)';
    }

    range.addEventListener('input', update);
    update();
}

// ══════════════════════════════════════════════
//  SIDEBAR
// ══════════════════════════════════════════════
const sidebar = document.getElementById('sidebar');

function toggleSidebar() {
    const exp = sidebar.classList.toggle('expanded');
    sidebar.querySelector('.sb-toggle-label').textContent = exp ? '收起' : '展开';
}

// ══════════════════════════════════════════════
//  PANEL STATE
// ══════════════════════════════════════════════
const pState = {
    play:     { maximized:false, minimized:false },
    settings: { maximized:false, minimized:false },
    user:     { maximized:false, minimized:false }
};

function getPanel(n) { return document.getElementById('panel-' + n); }
function getNav(n)   { return document.getElementById('nav-'   + n); }

function bringToFront(panel) {
    document.querySelectorAll('.float-panel').forEach(p => p.classList.remove('on-top'));
    panel.classList.add('on-top');
}

function openPanel(name) {
    const p = getPanel(name);
    p.classList.add('visible');
    getNav(name).classList.add('active');
    bringToFront(p);
    p.style.animation = 'panelIn 0.22s cubic-bezier(0.16,1,0.3,1)';
    p.addEventListener('animationend', () => p.style.animation = '', { once: true });
    refreshCursorTargets();
}

function closePanel(name) {
    const p = getPanel(name), st = pState[name];
    if (st.maximized) { p.classList.remove('maximized'); st.maximized = false; }
    if (st.minimized) { p.classList.remove('minimized'); st.minimized = false; }
    p.classList.remove('visible');
    getNav(name).classList.remove('active');
}

function minimizePanel(name) {
    const p = getPanel(name), st = pState[name];
    if (st.minimized) {
        p.classList.remove('minimized'); st.minimized = false;
    } else {
        if (st.maximized) restoreMax(p, name);
        p.classList.add('minimized'); st.minimized = true;
    }
}

function maximizePanel(name) {
    const p = getPanel(name), st = pState[name];
    if (st.maximized) {
        restoreMax(p, name);
    } else {
        if (st.minimized) { p.classList.remove('minimized'); st.minimized = false; }
        st.savedL = parseFloat(p.style.left) || 0;
        st.savedT = parseFloat(p.style.top)  || 0;
        st.savedW = p.offsetWidth;
        st.savedH = p.offsetHeight;
        withAnim(p, () => {
            p.style.left   = '0';
            p.style.top    = '0';
            p.style.width  = window.innerWidth  + 'px';
            p.style.height = window.innerHeight + 'px';
            p.style.transform = '';
        });
        st.maximized = true;
        bringToFront(p);
    }
}

function restoreMax(p, name) {
    const st = pState[name];
    withAnim(p, () => {
        p.style.left   = st.savedL + 'px';
        p.style.top    = st.savedT + 'px';
        p.style.width  = st.savedW + 'px';
        p.style.height = st.savedH + 'px';
        p.style.transform = '';
    });
    st.maximized = false;
}

function withAnim(p, fn) {
    p.classList.add('animating'); fn();
    const done = () => { p.classList.remove('animating'); p.removeEventListener('transitionend', done); };
    p.addEventListener('transitionend', done);
}

function togglePanel(name) {
    const p = getPanel(name);
    if (p.classList.contains('visible')) closePanel(name);
    else openPanel(name);
}

// ══════════════════════════════════════════════
//  DRAG — transform 方案，零 reflow
//  边界：标题栏至少留 40px 在视口内，其余可以拖出去
// ══════════════════════════════════════════════
const BAR_MIN = 40; // 标题栏最少露出 px
let activeDragPanel = null;

document.querySelectorAll('.panel-bar').forEach(bar => {
    const panel = bar.closest('.float-panel');
    const name  = panel.id.replace('panel-', '');

    bar.addEventListener('mousedown', e => {
        if (e.target.classList.contains('tl')) return;
        if (pState[name].maximized) return;
        commitTransform(panel);
        activeDragPanel = panel;
        panel._dragStartX = e.clientX;
        panel._dragStartY = e.clientY;
        bringToFront(panel);
        e.preventDefault();
    });

    panel.addEventListener('mousedown', () => bringToFront(panel));
});

function commitTransform(panel) {
    const m = (panel.style.transform || '').match(/translate\(([^,]+)px,\s*([^)]+)px\)/);
    if (!m) return;
    panel.style.left      = (parseFloat(panel.style.left) || 0) + parseFloat(m[1]) + 'px';
    panel.style.top       = (parseFloat(panel.style.top)  || 0) + parseFloat(m[2]) + 'px';
    panel.style.transform = '';
}

// ══════════════════════════════════════════════
//  RESIZE
// ══════════════════════════════════════════════
const MIN_W = 220, MIN_H = 80;
let activeResizeInfo = null;

document.querySelectorAll('.resize-handle').forEach(handle => {
    const panel = handle.closest('.float-panel');
    const name  = panel.id.replace('panel-', '');

    handle.addEventListener('mousedown', e => {
        if (pState[name].maximized || pState[name].minimized) return;
        commitTransform(panel);
        bringToFront(panel);
        activeResizeInfo = {
            panel, dir: handle.dataset.dir,
            startX: e.clientX, startY: e.clientY,
            startL: parseFloat(panel.style.left) || 0,
            startT: parseFloat(panel.style.top)  || 0,
            startW: panel.offsetWidth,
            startH: panel.offsetHeight
        };
        e.preventDefault(); e.stopPropagation();
    });
});

// ══════════════════════════════════════════════
//  全局 mousemove / mouseup
// ══════════════════════════════════════════════
document.addEventListener('mousemove', e => {

    // 拖拽
    if (activeDragPanel) {
        const dx = e.clientX - activeDragPanel._dragStartX;
        const dy = e.clientY - activeDragPanel._dragStartY;
        const l0 = parseFloat(activeDragPanel.style.left) || 0;
        const t0 = parseFloat(activeDragPanel.style.top)  || 0;
        const pw = activeDragPanel.offsetWidth;
        const ph = activeDragPanel.offsetHeight;
        const vw = window.innerWidth, vh = window.innerHeight;

        // 限制：标题栏（高38px）左边至少 BAR_MIN px 在视口内，顶部不超出，底部至少留 BAR_MIN
        const minDx = -(l0 + pw - BAR_MIN);          // 向右最多让左边缩到 BAR_MIN 露出
        const maxDx = vw - l0 - BAR_MIN;              // 向左最多让右边缩到 BAR_MIN 露出
        const minDy = -t0;                             // 顶部不超出视口
        const maxDy = vh - t0 - BAR_MIN;              // 底部至少 BAR_MIN 露出

        const cdx = Math.max(minDx, Math.min(dx, maxDx));
        const cdy = Math.max(minDy, Math.min(dy, maxDy));

        activeDragPanel.style.transform = `translate(${cdx}px,${cdy}px)`;
    }

    // 缩放
    if (activeResizeInfo) {
        const { panel, dir, startX, startY, startL, startT, startW, startH } = activeResizeInfo;
        const dx = e.clientX - startX, dy = e.clientY - startY;
        let nl = startL, nt = startT, nw = startW, nh = startH;

        if (dir.includes('e')) nw = Math.max(MIN_W, startW + dx);
        if (dir.includes('s')) nh = Math.max(MIN_H, startH + dy);
        if (dir.includes('w')) { nw = Math.max(MIN_W, startW - dx); nl = startL + startW - nw; }
        if (dir.includes('n')) { nh = Math.max(MIN_H, startH - dy); nt = startT + startH - nh; }

        panel.style.left   = nl + 'px';
        panel.style.top    = nt + 'px';
        panel.style.width  = nw + 'px';
        panel.style.height = nh + 'px';
    }

}, { passive: true });

document.addEventListener('mouseup', () => {
    if (activeDragPanel) { commitTransform(activeDragPanel); activeDragPanel = null; }
    activeResizeInfo = null;
});

// ══════════════════════════════════════════════
//  GAME LOGIC
// ══════════════════════════════════════════════
let requiredJavaVersion = 8;
const statusMsg = document.getElementById('statusMsg');
const launchBtn = document.getElementById('launchBtn');

fetch('/api/versions')
    .then(r => r.json())
    .then(data => buildVersionList(data))
    .catch(() => {
        document.getElementById('versionDisplay').textContent = '网络错误 / 离线模式';
        statusMsg.innerText = '版本加载失败';
    });

function launchGame() {
    const username = document.getElementById('username').value.trim();
    if (!username) { shakePanel('play'); return; }
    if (!selectedVersion) { statusMsg.innerText = '请先选择版本'; return; }

    setLoading(true);
    statusMsg.innerText = '正在初始化...';
    statusMsg.style.color = '';

    fetch('/api/launch', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, version: selectedVersion, memory: parseInt(document.getElementById('memory').value) })
    })
    .then(r => r.json())
    .then(data => {
        if (data.success) {
            statusMsg.innerText = '启动成功！'; statusMsg.style.color = 'var(--lime)';
        } else if (data.error === 'no_java') {
            statusMsg.innerText = '缺少 Java 环境'; statusMsg.style.color = 'var(--rust)';
            requiredJavaVersion = data.requiredVersion || 8;
            showJavaModal();
        } else {
            statusMsg.innerText = '错误：' + data.message; statusMsg.style.color = 'var(--rust)';
        }
    })
    .catch(() => { statusMsg.innerText = '连接失败'; statusMsg.style.color = 'var(--rust)'; })
    .finally(() => setLoading(false));
}

function setLoading(on) {
    launchBtn.disabled = on;
    launchBtn.innerHTML = on
        ? '处理中...'
        : '<svg style="width:14px;height:14px;fill:currentColor" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg> 启动游戏';
}

function shakePanel(name) {
    const p = getPanel(name); commitTransform(p);
    const seq = [5,-5,4,-4,2,-2,0];
    seq.forEach((v, i) => setTimeout(() => p.style.transform = `translateX(${v}px)`, i * 45));
    setTimeout(() => { p.style.transform = ''; commitTransform(p); }, seq.length * 45 + 50);
}

// Java Modal
const javaModal = document.getElementById('javaModal');

function showJavaModal() {
    document.getElementById('targetJavaVer').innerText        = requiredJavaVersion;
    document.getElementById('javaProgressUI').style.display   = 'none';
    document.getElementById('javaModalActions').style.display = 'flex';
    javaModal.style.display = 'flex';
}

function closeJavaModal() {
    javaModal.style.display = 'none';
}

function confirmInstallJava() {
    document.getElementById('javaModalActions').style.display = 'none';
    document.getElementById('javaProgressUI').style.display   = 'block';
    
    // Reset UI
    document.getElementById('javaProgressBar').style.width = '0%';
    document.getElementById('javaPercent').innerText = '0%';
    document.getElementById('javaStatusText').innerText = '正在请求...';
    document.getElementById('javaStatusText').style.color = '';

    fetch('/api/java/install', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ version: requiredJavaVersion })
    })
    .then(r => r.json())
    .then(d => {
        if (!d.success) { 
            document.getElementById('javaStatusText').innerText = '请求失败: ' + d.message; 
            document.getElementById('javaStatusText').style.color = 'var(--rust)';
        }
    });
}

// WebSocket
let ws = null;
function connectWS() {
    ws = new WebSocket('ws://localhost:8081');
    ws.onopen = () => console.log('WS Connected');
    ws.onmessage = (e) => {
        try {
            const msg = JSON.parse(e.data);
            if (msg.type === 'java_progress') {
                const pct = msg.percent + '%';
                document.getElementById('javaProgressBar').style.width = pct;
                document.getElementById('javaPercent').innerText = pct;
                document.getElementById('javaStatusText').innerText = msg.message;
            } else if (msg.type === 'java_finished') {
                if (msg.success) {
                    document.getElementById('javaProgressBar').style.width = '100%';
                    document.getElementById('javaStatusText').innerText = '完成！正在启动...';
                    document.getElementById('javaStatusText').style.color = 'var(--lime)';
                    setTimeout(() => { closeJavaModal(); launchGame(); }, 1000);
                } else {
                    document.getElementById('javaStatusText').innerText = '错误：' + msg.error;
                    document.getElementById('javaStatusText').style.color = 'var(--rust)';
                }
            }
        } catch(e) { console.error(e); }
    };
    ws.onclose = () => setTimeout(connectWS, 2000); // Auto reconnect
}

// ── Init ──
initSlider();
refreshCursorTargets();
connectWS();
</script>
</body>
</html>
)html";
