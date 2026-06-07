/* =========================================================
   AI聊天助手 · 放学后茶会 (Houkago Chat)
   client logic: state, API, SSE streaming, markdown
   ========================================================= */

'use strict';

const API_BASE = '';   // same origin

/* ---------- state ---------- */
const state = {
    sessions: [],
    activeSessionId: null,
    activeModel: '',
    streaming: false,
    selectedModel: null,
    availableModels: [],
    historyCache: new Map(),   // sessionId -> messages
};

/* ---------- dom refs ---------- */
const $ = (id) => document.getElementById(id);

const dom = {
    newChatBtn:        $('newChatBtn'),
    emptyNewChatBtn:   $('emptyNewChatBtn'),
    sessionList:       $('sessionList'),
    emptySessions:     $('emptySessions'),
    emptyState:        $('emptyState'),
    chatView:          $('chatView'),
    chatTitle:         $('chatTitle'),
    chatModelBadge:    $('chatModelBadge'),
    chatStatus:        $('chatStatus'),
    chatMessages:      $('chatMessages'),
    messageInput:      $('messageInput'),
    sendBtn:           $('sendBtn'),
    charCounter:       $('charCounter'),
    modelModal:        $('modelModal'),
    modelGrid:         $('modelGrid'),
    modelLoading:      $('modelLoading'),
    modalCancelBtn:    $('modalCancelBtn'),
    modalConfirmBtn:   $('modalConfirmBtn'),
    toastStack:        $('toastStack'),
};

/* =========================================================
   utilities
   ========================================================= */
function escapeHtml(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function fmtTime(ts) {
    if (!ts) return '';
    const d = new Date(ts * 1000);
    const today = new Date();
    const pad = (n) => String(n).padStart(2, '0');
    const sameDay = d.toDateString() === today.toDateString();
    if (sameDay) return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
    return `${d.getMonth() + 1}月${d.getDate()}日 ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

function fmtRelative(ts) {
    if (!ts) return '';
    const diff = Date.now() / 1000 - ts;
    if (diff < 60) return '刚刚';
    if (diff < 3600) return `${Math.floor(diff / 60)} 分钟前`;
    if (diff < 86400) return `${Math.floor(diff / 3600)} 小时前`;
    if (diff < 86400 * 7) return `${Math.floor(diff / 86400)} 天前`;
    const d = new Date(ts * 1000);
    return `${d.getMonth() + 1}/${d.getDate()}`;
}

function toast(msg, type = 'info', ttl = 2600) {
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    const icon = type === 'success' ? '♪' : type === 'error' ? '!' : '♫';
    el.innerHTML = `<span style="color:var(--accent-pink-deep);font-family:var(--font-display);font-size:18px;">${icon}</span><span>${escapeHtml(msg)}</span>`;
    dom.toastStack.appendChild(el);
    setTimeout(() => {
        el.classList.add('removing');
        setTimeout(() => el.remove(), 250);
    }, ttl);
}

/* =========================================================
   API
   ========================================================= */
async function api(path, opts = {}) {
    const resp = await fetch(API_BASE + path, {
        headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) },
        ...opts,
    });
    if (!resp.ok) {
        let msg = `HTTP ${resp.status}`;
        try { const j = await resp.json(); if (j.message) msg = j.message; } catch (_) {}
        throw new Error(msg);
    }
    return resp.json();
}

const apiGetSessions      = ()           => api('/api/sessions');
const apiGetModels        = ()           => api('/api/models');
const apiCreateSession    = (model)      => api('/api/session', { method: 'POST', body: JSON.stringify({ model }) });
const apiDeleteSession    = (id)         => api('/api/session/' + encodeURIComponent(id), { method: 'DELETE' });
const apiGetHistory       = (id)         => api(`/api/session/${encodeURIComponent(id)}/history`);

/* =========================================================
   SSE streaming
   ========================================================= */
function parseSSEData(raw) {
    if (raw === '[DONE]') return { done: true };
    // try JSON: handles contract B (object) and contract C (string literal)
    try {
        const parsed = JSON.parse(raw);
        if (typeof parsed === 'string')         return { text: parsed };
        if (parsed && typeof parsed === 'object') return { text: parsed.delta ?? parsed.content ?? parsed.text ?? '' };
    } catch (_) { /* fall through: contract A raw text */ }
    return { text: raw };
}

async function streamChat(sessionId, message, { onChunk, onDone, onError, signal }) {
    let resp;
    try {
        resp = await fetch(API_BASE + '/api/message/async', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Accept': 'text/event-stream' },
            body: JSON.stringify({ session_id: sessionId, message }),
            signal,
        });
    } catch (e) { onError(e); return; }

    if (!resp.ok || !resp.body) { onError(new Error(`HTTP ${resp.status}`)); return; }

    const reader = resp.body.getReader();
    const decoder = new TextDecoder('utf-8');
    let buffer = '';
    try {
        for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            buffer += decoder.decode(value, { stream: true });

            let idx;
            while ((idx = buffer.indexOf('\n\n')) !== -1) {
                const eventChunk = buffer.slice(0, idx);
                buffer = buffer.slice(idx + 2);
                // event may have multiple lines, find data: lines
                for (const line of eventChunk.split('\n')) {
                    if (!line.startsWith('data:')) continue;
                    const raw = line.slice(5).replace(/^\s/, '');
                    const parsed = parseSSEData(raw);
                    if (parsed.done) { onDone(); return; }
                    if (parsed.text) onChunk(parsed.text);
                }
            }
        }
    } catch (e) {
        onError(e); return;
    }
    onDone();
}

/* =========================================================
   sidebar / session list
   ========================================================= */
async function loadSessions() {
    try {
        const r = await apiGetSessions();
        state.sessions = (r.data || []).sort((a, b) => (b.updated_at || 0) - (a.updated_at || 0));
        renderSessionList();
    } catch (e) {
        toast('加载会话列表失败: ' + e.message, 'error');
    }
}

function renderSessionList() {
    const list = dom.sessionList;
    list.innerHTML = '';
    if (!state.sessions.length) {
        dom.emptySessions.hidden = false;
        return;
    }
    dom.emptySessions.hidden = true;
    for (const s of state.sessions) {
        const li = document.createElement('li');
        li.className = 'session-item' + (s.id === state.activeSessionId ? ' active' : '');
        li.dataset.id = s.id;
        const msgPreview = s.first_user_message || '（暂无消息）';
        li.innerHTML = `
            <div class="session-item-time">${escapeHtml(fmtRelative(s.updated_at))}</div>
            <div class="session-item-msg">${escapeHtml(msgPreview)}</div>
            <span class="session-item-model">${escapeHtml(s.model || '')}</span>
            <button class="session-item-delete" title="删除会话" aria-label="删除会话">···</button>
        `;
        li.addEventListener('click', (ev) => {
            if (ev.target.closest('.session-item-delete')) return;
            openSession(s.id, s.model);
        });
        li.querySelector('.session-item-delete').addEventListener('click', (ev) => {
            ev.stopPropagation();
            handleDeleteSession(s.id);
        });
        list.appendChild(li);
    }
}

async function handleDeleteSession(id) {
    if (!confirm('确定删除这条对话吗？\n这首歌就会被永久封存哦～')) return;
    try {
        await apiDeleteSession(id);
        toast('已删除会话', 'success');
        state.historyCache.delete(id);
        if (state.activeSessionId === id) {
            state.activeSessionId = null;
            showEmptyState();
        }
        await loadSessions();
    } catch (e) {
        toast('删除失败: ' + e.message, 'error');
    }
}

/* =========================================================
   chat view
   ========================================================= */
function showEmptyState() {
    dom.emptyState.hidden = false;
    dom.chatView.hidden = true;
}

function showChatView() {
    dom.emptyState.hidden = true;
    dom.chatView.hidden = false;
}

async function openSession(id, model) {
    state.activeSessionId = id;
    state.activeModel = model || '';
    showChatView();
    renderSessionList();   // update active highlight
    dom.chatTitle.textContent = ' ';
    dom.chatModelBadge.textContent = model || '';
    setChatStatus('加载中...', 'thinking');
    dom.chatMessages.innerHTML = '';

    let messages;
    if (state.historyCache.has(id)) {
        messages = state.historyCache.get(id);
    } else {
        try {
            const r = await apiGetHistory(id);
            messages = r.data || [];
            state.historyCache.set(id, messages);
        } catch (e) {
            toast('加载历史失败: ' + e.message, 'error');
            messages = [];
        }
    }

    // derive title from first user message
    const firstUserMsg = messages.find(m => m.role === 'user');
    dom.chatTitle.textContent = firstUserMsg ? firstUserMsg.content.slice(0, 40) : '新对话';

    for (const m of messages) appendMessage(m.role, m.content, m.timestamp);
    setChatStatus('就绪', 'ok');
    scrollMessagesToBottom();
    dom.messageInput.focus();
}

function setChatStatus(text, kind = 'ok') {
    dom.chatStatus.textContent = text;
    dom.chatStatus.className = 'chat-status' + (kind === 'thinking' ? ' thinking' : kind === 'error' ? ' error' : '');
}

function scrollMessagesToBottom(smooth = false) {
    requestAnimationFrame(() => {
        dom.chatMessages.scrollTo({
            top: dom.chatMessages.scrollHeight,
            behavior: smooth ? 'smooth' : 'auto',
        });
    });
}

/* ---------- markdown rendering ---------- */
function renderMarkdown(text) {
    if (typeof marked === 'undefined') return escapeHtml(text).replace(/\n/g, '<br>');
    try {
        marked.setOptions({
            breaks: true,
            gfm: true,
            highlight: (code, lang) => {
                if (typeof hljs === 'undefined') return escapeHtml(code);
                if (lang && hljs.getLanguage(lang)) {
                    try { return hljs.highlight(code, { language: lang }).value; } catch (_) {}
                }
                try { return hljs.highlightAuto(code).value; } catch (_) {}
                return escapeHtml(code);
            },
        });
        return marked.parse(text);
    } catch (_) {
        return escapeHtml(text).replace(/\n/g, '<br>');
    }
}

function enhanceCodeBlocks(rootEl) {
    rootEl.querySelectorAll('pre > code').forEach((codeEl) => {
        const pre = codeEl.parentElement;
        if (pre.parentElement && pre.parentElement.classList.contains('code-block')) return;
        const wrapper = document.createElement('div');
        wrapper.className = 'code-block';
        // detect language
        let lang = '';
        for (const cls of codeEl.classList) {
            if (cls.startsWith('language-')) { lang = cls.slice('language-'.length); break; }
            if (cls === 'hljs') continue;
        }
        const header = document.createElement('div');
        header.className = 'code-block-header';
        header.innerHTML = `<span class="code-lang">${escapeHtml(lang || 'text')}</span>
                            <button class="code-copy-btn" type="button">⧉ 复制</button>`;
        const copyBtn = header.querySelector('.code-copy-btn');
        copyBtn.addEventListener('click', async () => {
            const text = codeEl.textContent;
            try {
                await navigator.clipboard.writeText(text);
                copyBtn.textContent = '✓ 已复制';
                copyBtn.classList.add('copied');
                setTimeout(() => {
                    copyBtn.textContent = '⧉ 复制';
                    copyBtn.classList.remove('copied');
                }, 1500);
            } catch (_) {
                toast('复制失败', 'error');
            }
        });
        pre.parentElement.insertBefore(wrapper, pre);
        wrapper.appendChild(header);
        wrapper.appendChild(pre);
    });
}

/* ---------- message bubbles ---------- */
function appendMessage(role, content, ts) {
    const wrap = document.createElement('div');
    wrap.className = `msg ${role === 'user' ? 'user' : 'assistant'}`;
    const avatarChar = role === 'user' ? 'U' : 'A';
    const time = ts ? fmtTime(ts) : '';

    wrap.innerHTML = `
        <div class="msg-avatar">${avatarChar}</div>
        <div class="msg-body">
            <div class="msg-bubble"></div>
            <div class="msg-time">${escapeHtml(time)}</div>
        </div>
    `;
    const bubble = wrap.querySelector('.msg-bubble');
    if (role === 'assistant') {
        bubble.innerHTML = renderMarkdown(content);
        enhanceCodeBlocks(bubble);
    } else {
        // user message: escape (no markdown for user input)
        bubble.textContent = content;
    }
    dom.chatMessages.appendChild(wrap);
    return { wrap, bubble };
}

/* =========================================================
   send + stream
   ========================================================= */
let renderTimer = null;

async function handleSend() {
    if (state.streaming) return;
    if (!state.activeSessionId) { toast('请先选择或创建一个会话', 'error'); return; }
    const text = dom.messageInput.value.trim();
    if (!text) return;

    // append user bubble
    const nowTs = Math.floor(Date.now() / 1000);
    appendMessage('user', text, nowTs);

    // append assistant placeholder
    const { wrap: aWrap, bubble: aBubble } = appendMessage('assistant', '', nowTs);
    aBubble.classList.add('streaming');

    // clear input
    dom.messageInput.value = '';
    syncInputUI();
    scrollMessagesToBottom(true);

    state.streaming = true;
    setChatStatus('思考中...', 'thinking');
    dom.sendBtn.disabled = true;
    dom.sendBtn.classList.add('sending');

    let accumulator = '';
    const flushRender = () => {
        aBubble.innerHTML = renderMarkdown(accumulator);
        aBubble.classList.add('streaming');
        enhanceCodeBlocks(aBubble);
        scrollMessagesToBottom();
    };
    const scheduleRender = () => {
        if (renderTimer) return;
        renderTimer = setTimeout(() => { renderTimer = null; flushRender(); }, 60);
    };

    await streamChat(state.activeSessionId, text, {
        onChunk(t) { accumulator += t; scheduleRender(); },
        onDone() {
            if (renderTimer) { clearTimeout(renderTimer); renderTimer = null; }
            aBubble.innerHTML = renderMarkdown(accumulator || '*(空响应)*');
            aBubble.classList.remove('streaming');
            enhanceCodeBlocks(aBubble);
            // refresh time
            const tEl = aWrap.querySelector('.msg-time');
            tEl.textContent = fmtTime(Math.floor(Date.now() / 1000));
            state.streaming = false;
            setChatStatus('就绪', 'ok');
            dom.sendBtn.disabled = false;
            dom.sendBtn.classList.remove('sending');
            scrollMessagesToBottom(true);
            // refresh side list (message_count / time changed)
            loadSessions();
            // invalidate history cache
            state.historyCache.delete(state.activeSessionId);
            syncInputUI();
        },
        onError(e) {
            if (renderTimer) { clearTimeout(renderTimer); renderTimer = null; }
            aBubble.classList.remove('streaming');
            aBubble.innerHTML = `<span style="color:#ED6F6F">⚠ 出错了: ${escapeHtml(e.message || String(e))}</span>`;
            state.streaming = false;
            setChatStatus('出错', 'error');
            dom.sendBtn.disabled = false;
            dom.sendBtn.classList.remove('sending');
        },
    });
}

/* =========================================================
   input UI sync
   ========================================================= */
function syncInputUI() {
    const v = dom.messageInput.value;
    const len = [...v].length;   // count code-points so emojis = 1
    dom.charCounter.innerHTML = `<b>${len}</b> / 2000`;
    dom.charCounter.classList.toggle('near-limit', len >= 1800 && len < 2000);
    dom.charCounter.classList.toggle('at-limit',  len >= 2000);
    dom.sendBtn.disabled = state.streaming || !v.trim();
    // auto-resize textarea
    dom.messageInput.style.height = 'auto';
    dom.messageInput.style.height = Math.min(dom.messageInput.scrollHeight, 180) + 'px';
}

/* =========================================================
   model modal
   ========================================================= */
async function openModelModal() {
    state.selectedModel = null;
    dom.modelGrid.innerHTML = '';
    dom.modelGrid.style.display = 'none';
    dom.modelLoading.style.display = 'flex';
    dom.modalConfirmBtn.disabled = true;
    dom.modelModal.hidden = false;
    try {
        const r = await apiGetModels();
        state.availableModels = r.data || [];
        renderModelGrid();
    } catch (e) {
        toast('获取模型列表失败: ' + e.message, 'error');
        closeModelModal();
    }
}

function renderModelGrid() {
    dom.modelLoading.style.display = 'none';
    dom.modelGrid.style.display = 'grid';
    dom.modelGrid.innerHTML = '';
    if (!state.availableModels.length) {
        dom.modelGrid.innerHTML = `<div style="grid-column:1/-1;text-align:center;color:var(--text-muted);padding:24px;">乐队成员都还在练习中...</div>`;
        return;
    }
    for (const m of state.availableModels) {
        const card = document.createElement('button');
        card.type = 'button';
        card.className = 'model-card';
        card.dataset.name = m.name;
        card.innerHTML = `
            <span class="model-card-radio" aria-hidden="true"></span>
            <div class="model-card-name">${escapeHtml(m.name)}</div>
            <div class="model-card-desc">${escapeHtml(m.desc || '')}</div>
        `;
        card.addEventListener('click', () => selectModelCard(m.name));
        dom.modelGrid.appendChild(card);
    }
}

function selectModelCard(name) {
    state.selectedModel = name;
    dom.modelGrid.querySelectorAll('.model-card').forEach(c => {
        c.classList.toggle('selected', c.dataset.name === name);
    });
    dom.modalConfirmBtn.disabled = false;
}

function closeModelModal() {
    dom.modelModal.hidden = true;
    state.selectedModel = null;
}

async function confirmCreateSession() {
    if (!state.selectedModel) return;
    const model = state.selectedModel;
    dom.modalConfirmBtn.disabled = true;
    try {
        const r = await apiCreateSession(model);
        const sid = r.data && r.data.session_id;
        if (!sid) throw new Error('服务器未返回 session_id');
        toast('新对话已创建 ♪', 'success');
        closeModelModal();
        await loadSessions();
        openSession(sid, model);
    } catch (e) {
        toast('创建会话失败: ' + e.message, 'error');
        dom.modalConfirmBtn.disabled = false;
    }
}

/* =========================================================
   event wiring
   ========================================================= */
function bindEvents() {
    dom.newChatBtn.addEventListener('click', openModelModal);
    dom.emptyNewChatBtn.addEventListener('click', openModelModal);
    dom.modalCancelBtn.addEventListener('click', closeModelModal);
    dom.modalConfirmBtn.addEventListener('click', confirmCreateSession);
    dom.modelModal.addEventListener('click', (e) => {
        if (e.target === dom.modelModal) closeModelModal();
    });

    dom.messageInput.addEventListener('input', syncInputUI);
    dom.messageInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
            e.preventDefault();
            handleSend();
        }
    });
    dom.sendBtn.addEventListener('click', handleSend);

    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !dom.modelModal.hidden) closeModelModal();
    });
}

/* =========================================================
   bootstrap
   ========================================================= */
async function bootstrap() {
    bindEvents();
    syncInputUI();
    showEmptyState();
    await loadSessions();
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', bootstrap);
} else {
    bootstrap();
}
