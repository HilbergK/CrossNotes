// ── State ──────────────────────────────────────────────────────────────────
let books = [];
let currentBookPath = null;
let currentHighlights = [];

// ── Utility ────────────────────────────────────────────────────────────────
function showMessage(text, type) {
  const el = document.getElementById('message');
  el.textContent = text;
  el.className = 'message ' + type;
  setTimeout(() => { el.className = 'message'; }, 4000);
}

function noteId(h) {
  return `note_${h.spineIndex}_${h.startPage}_${h.startWordIndex}`;
}

// ── Tabs (Session 8) ─────────────────────────────────────────────────────────
function switchTab(tabName, btn) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(content => content.style.display = 'none');

  btn.classList.add('active');
  document.getElementById('tab-' + tabName).style.display = 'block';

  if (tabName === 'screenshots') {
    loadScreenshots();
  }
}

// ── Screenshots (Session 8) ──────────────────────────────────────────────────
// Reader screenshots are saved per-book under /screenshots/<BookTitle>/, while
// home/menu screenshots land flat in /screenshots/. Recurse one level so both show up.
async function scanScreenshots(dirPath, depth = 0) {
  if (depth > 2) return [];
  const res = await fetch('/api/files?path=' + encodeURIComponent(dirPath));
  if (!res.ok) return [];
  const entries = await res.json();
  const results = [];
  for (const entry of entries) {
    const fullPath = (dirPath === '/' ? '' : dirPath) + '/' + entry.name;
    if (entry.isDirectory) {
      const sub = await scanScreenshots(fullPath, depth + 1);
      results.push(...sub);
    } else if (entry.name.toLowerCase().endsWith('.bmp')) {
      results.push({ name: entry.name, path: fullPath });
    }
  }
  return results;
}

async function loadScreenshots() {
  const container = document.getElementById('screenshot-gallery');
  try {
    const images = await scanScreenshots('/screenshots');

    if (images.length === 0) {
      container.innerHTML = '<p class="empty-hint">No screenshots found.</p>';
      return;
    }

    container.innerHTML = '';
    images.forEach(img => {
      const card = document.createElement('div');
      card.className = 'screenshot-card';
      const downloadUrl = '/download?path=' + encodeURIComponent(img.path);

      const imgEl = document.createElement('img');
      imgEl.src = downloadUrl;
      imgEl.loading = 'lazy';
      imgEl.alt = 'Screenshot';
      card.appendChild(imgEl);

      const actions = document.createElement('div');
      actions.className = 'screenshot-actions';

      const downloadLink = document.createElement('a');
      downloadLink.href = downloadUrl;
      downloadLink.download = img.name;
      downloadLink.textContent = 'Download';
      actions.appendChild(downloadLink);

      const deleteBtn = document.createElement('button');
      deleteBtn.className = 'btn-delete-screenshot';
      deleteBtn.textContent = 'Delete';
      deleteBtn.addEventListener('click', () => deleteScreenshot(img.path, card));
      actions.appendChild(deleteBtn);

      card.appendChild(actions);
      container.appendChild(card);
    });
  } catch (e) {
    container.innerHTML = '<p class="empty-hint" style="color:var(--danger-color);">Could not load screenshots.</p>';
  }
}

// No confirmation by design — screenshots are low-stakes and easy to retake.
async function deleteScreenshot(path, cardEl) {
  try {
    const res = await fetch('/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'path=' + encodeURIComponent(path)
    });
    if (!res.ok) throw new Error(await res.text());
    cardEl.remove();
  } catch (e) {
    showMessage('Failed to delete screenshot: ' + e.message, 'error');
  }
}

// ── Book list ───────────────────────────────────────────────────────────────
// Recursively walk a directory via /api/files and collect all EPUB file paths.
async function scanDir(dirPath, depth = 0) {
  if (depth > 5) return [];
  const res = await fetch('/api/files?path=' + encodeURIComponent(dirPath));
  if (!res.ok) return [];
  const entries = await res.json();
  const results = [];
  for (const entry of entries) {
    const fullPath = (dirPath === '/' ? '' : dirPath) + '/' + entry.name;
    if (entry.isDirectory) {
      const sub = await scanDir(fullPath, depth + 1);
      results.push(...sub);
    } else if (entry.isEpub) {
      results.push({ name: entry.name, path: fullPath });
    }
  }
  return results;
}

async function loadBookList() {
  const container = document.getElementById('book-list');
  try {
    const epubs = await scanDir('/');

    if (epubs.length === 0) {
      container.innerHTML = '<p style="font-size:0.85em;color:var(--label-color);padding:8px 0;">No EPUB files found on device.</p>';
      return;
    }

    books = epubs;
    renderBookList();
  } catch (e) {
    container.innerHTML = '<p style="font-size:0.85em;color:var(--danger-color);">Could not load books.</p>';
    console.error(e);
  }
}

function renderBookList() {
  const container = document.getElementById('book-list');
  container.innerHTML = '';
  books.forEach(book => {
    const div = document.createElement('div');
    div.className = 'book-item' + (book.path === currentBookPath ? ' active' : '');
    div.dataset.path = book.path;

    const name = book.name.replace(/\.epub$/i, '');
    div.innerHTML = `<span class="book-title">${escapeHtml(name)}</span>`;
    div.addEventListener('click', () => selectBook(book.path));
    container.appendChild(div);
  });
}

// ── Select book ─────────────────────────────────────────────────────────────
async function selectBook(path) {
  currentBookPath = path;
  renderBookList();

  const main = document.getElementById('highlights-main');
  const emptyState = document.getElementById('highlights-empty-state');
  const toolbar = document.getElementById('highlights-toolbar');
  const highlightsContainer = document.getElementById('highlights-container');

  emptyState.style.display = 'none';
  toolbar.style.display = 'flex';
  highlightsContainer.style.display = 'block';
  highlightsContainer.innerHTML = '<div class="loader-container"><span class="loader"></span></div>';

  try {
    const res = await fetch('/api/highlights?path=' + encodeURIComponent(path));
    if (!res.ok) throw new Error('Failed to load highlights');
    const highlights = await res.json();
    currentHighlights = highlights;
    renderHighlights(highlights);
  } catch (e) {
    highlightsContainer.innerHTML = '<p class="empty-hint" style="color:var(--danger-color);">Could not load highlights for this book.</p>';
    console.error(e);
  }
}

// ── Export notes ─────────────────────────────────────────────────────────────
function currentBookTitle() {
  const book = books.find(b => b.path === currentBookPath);
  const name = book ? book.name : (currentBookPath.split('/').pop() || 'book');
  return name.replace(/\.epub$/i, '');
}

function exportNotes() {
  if (!currentHighlights || currentHighlights.length === 0) {
    showMessage('No highlights to export.', 'error');
    return;
  }

  const title = currentBookTitle();
  const lines = [`# ${title}`, ''];

  currentHighlights.forEach(h => {
    const chapter = h.chapterTitle || 'Unknown Chapter';
    const tag = (h.note && h.note.tag) ? ` (${h.note.tag})` : '';
    lines.push(`## ${chapter}${tag}`);
    lines.push('');
    lines.push(`> ${h.text}`);
    if (h.note && h.note.text) {
      lines.push('');
      lines.push(`**Note:** ${h.note.text}`);
    }
    lines.push('');
    lines.push('---');
    lines.push('');
  });

  const blob = new Blob([lines.join('\n')], { type: 'text/markdown' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = (title.replace(/[^\w\- ]/g, '').trim() || 'book') + ' - notes.md';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

// Same symbol set and order as the on-device tag picker.
const TAG_OPTIONS = ['', '!', '?', '*', '~', '+', '=', '#', '<', '>'];

function tagOptionsHtml(selectedTag) {
  return TAG_OPTIONS.map(t => {
    const label = t === '' ? 'No tag' : t;
    const selected = (selectedTag || '') === t ? ' selected' : '';
    return `<option value="${t}"${selected}>${escapeHtml(label)}</option>`;
  }).join('');
}

// ── Render highlights ────────────────────────────────────────────────────────
function renderHighlights(highlights) {
  const container = document.getElementById('highlights-container');
  container.innerHTML = '';

  if (highlights.length === 0) {
    container.innerHTML = '<p class="empty-hint">No highlights found for this book.</p>';
    return;
  }

  highlights.forEach((h, idx) => {
    const card = document.createElement('div');
    card.className = 'highlight-card';
    card.id = 'card_' + idx;

    const chapter = h.chapterTitle || 'Unknown Chapter';
    const noteText = (h.note && h.note.text) ? h.note.text : '';
    const currentTag = (h.note && h.note.tag) ? h.note.tag : '';
    // Session 6: on-device tag (!, ?, >, <, *, ~, +, =, #) shown as a badge before the chapter.
    const tagBadge = currentTag ? `<span class="note-tag">${escapeHtml(currentTag)}</span>` : '';

    card.innerHTML = `
      <div class="highlight-meta" id="meta_${idx}"><span id="tagbadge_${idx}">${tagBadge}</span>${escapeHtml(chapter)}</div>
      <blockquote class="highlight-text">${escapeHtml(h.text)}</blockquote>
      <div class="tag-row">
        <label for="tag_${idx}">Tag</label>
        <select class="tag-select" id="tag_${idx}" data-idx="${idx}" onchange="saveNote(${idx})">
          ${tagOptionsHtml(currentTag)}
        </select>
      </div>
      <label class="note-label" for="${noteId(h)}">Your Note</label>
      <textarea
        class="note-textarea"
        id="${noteId(h)}"
        data-idx="${idx}"
        placeholder="Add a note…"
        rows="3"
      >${escapeHtml(noteText)}</textarea>
      <div class="note-actions">
        <span class="save-status" id="status_${idx}">Saved</span>
        <button class="btn-delete-note" onclick="deleteNoteConfirm(${idx})">Delete</button>
        <button class="btn-clear-note" onclick="clearNote(${idx})">Clear</button>
        <button class="btn-save-note" onclick="saveNote(${idx})">Save Note</button>
      </div>
    `;
    container.appendChild(card);
  });
}

// ── Save note (and tag) ─────────────────────────────────────────────────────
async function saveNote(idx) {
  const h = currentHighlights[idx];
  const textarea = document.getElementById(noteId(h));
  const tagSelect = document.getElementById('tag_' + idx);
  const statusEl = document.getElementById('status_' + idx);
  const btn = textarea.closest('.highlight-card').querySelector('.btn-save-note');

  const tagValue = tagSelect ? tagSelect.value : '';

  btn.disabled = true;
  try {
    const res = await fetch('/api/notes', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        path: currentBookPath,
        spineIndex: h.spineIndex,
        startPage: h.startPage,
        startWordIndex: h.startWordIndex,
        timestamp: h.timestamp,
        text: textarea.value.trim(),
        tag: tagValue
      })
    });
    if (!res.ok) throw new Error(await res.text());

    // Update local state
    if (!currentHighlights[idx].note) {
      currentHighlights[idx].note = {};
    }
    currentHighlights[idx].note.text = textarea.value.trim();
    currentHighlights[idx].note.tag = tagValue;

    const badgeEl = document.getElementById('tagbadge_' + idx);
    if (badgeEl) {
      badgeEl.innerHTML = tagValue ? `<span class="note-tag">${escapeHtml(tagValue)}</span>` : '';
    }

    statusEl.classList.add('visible');
    setTimeout(() => statusEl.classList.remove('visible'), 2500);
  } catch (e) {
    showMessage('Failed to save note: ' + e.message, 'error');
  } finally {
    btn.disabled = false;
  }
}

// ── Clear note ───────────────────────────────────────────────────────────────
async function clearNote(idx) {
  const h = currentHighlights[idx];
  const textarea = document.getElementById(noteId(h));
  textarea.value = '';
  await saveNote(idx);
}

// Clears both text and tag, which the server treats as a full delete.
async function deleteNoteConfirm(idx) {
  if (!confirm('Delete this note and its tag?')) return;
  const h = currentHighlights[idx];
  const textarea = document.getElementById(noteId(h));
  const tagSelect = document.getElementById('tag_' + idx);
  textarea.value = '';
  if (tagSelect) tagSelect.value = '';
  await saveNote(idx);
}

// ── HTML escaping ─────────────────────────────────────────────────────────────
function escapeHtml(str) {
  if (str === null || str === undefined) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

// ── Init ──────────────────────────────────────────────────────────────────────
// If the URL contains #screenshots (e.g. from the Screenshots QR code shortcut),
// open directly on the Screenshots tab. Otherwise default to the Notes tab.
(function () {
  if (window.location.hash === '#screenshots') {
    const btn = document.querySelector('.tab-btn[onclick*="screenshots"]');
    if (btn) switchTab('screenshots', btn);
  }
})();

loadBookList();
