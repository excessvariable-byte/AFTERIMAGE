/**
 * AFTERIMAGE 2.0 — Human Memory Retrieval Web Application
 */

const $ = s => document.querySelector(s);
const $$ = s => document.querySelectorAll(s);

// Global State
let currentView = 'search';
let isBusy = false;
let currentModalDoc = null;
let currentModalPage = 1;
let progressPollingInterval = null;
let libraryCurrentPage = 1;

// Helper: Escape HTML
function esc(str = '') {
  return String(str).replace(/[&<>"']/g, c => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  }[c]));
}

// Toast Notification
function toast(msg, duration = 2800) {
  const t = $('#toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), duration);
}

// Format bytes to human readable
function formatBytes(bytes) {
  if (!bytes || bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

// API Helper
async function api(url, method = 'GET', data = null) {
  const options = {
    method,
    headers: { 'Content-Type': 'application/json' }
  };
  if (data) options.body = JSON.stringify(data);
  const res = await fetch(url, options);
  const json = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new Error(json.error || `Request failed with status ${res.status}`);
  }
  return json;
}

// ---------------------------------------------------------------------------
// View Navigation
// ---------------------------------------------------------------------------

function setView(viewName) {
  currentView = viewName;
  $$('.nav').forEach(n => n.classList.toggle('active', n.dataset.view === viewName));

  $('#view-search').classList.toggle('hidden', viewName !== 'search');
  $('#view-library').classList.toggle('hidden', viewName !== 'library');
  $('#view-developer').classList.toggle('hidden', viewName !== 'developer');

  const titles = {
    search: 'MEMORY SEARCH',
    library: 'YOUR LIBRARY',
    developer: 'DEVELOPER CONSOLE'
  };
  $('#crumb-current').textContent = titles[viewName] || 'WORKSPACE';

  if (viewName === 'library') {
    loadLibraryTable();
    checkIndexingProgress();
  } else if (viewName === 'developer') {
    loadDevStats();
  }
}

$$('.nav').forEach(btn => {
  btn.addEventListener('click', () => setView(btn.dataset.view));
});

// ---------------------------------------------------------------------------
// Memory Search
// ---------------------------------------------------------------------------

async function executeSearch() {
  const query = $('#query').value.trim();
  if (!query || isBusy) return;

  isBusy = true;
  $('#search-btn').disabled = true;
  $('#search-btn').textContent = 'Retrieving…';

  $('#hero').classList.add('hidden');
  $('#results-section').classList.remove('hidden');
  $('#results-title').textContent = 'Searching your memory trail…';
  $('#results-subtitle').textContent = `Analyzing recollection: “${query}”`;
  $('#results').innerHTML = '<div class="empty">Scanning associations, temporal timestamps, and concepts...</div>';
  $('#related-block').classList.add('hidden');
  $('#memory-interpretation').classList.add('hidden');

  const filters = {
    date_range: $('#filter-date').value,
    file_type: $('#format-pills .pill.active')?.dataset.type || 'all'
  };

  try {
    const data = await api('/api/search', 'POST', { query, filters });

    // 1. Show Memory Interpretation Bar
    if (data.interpretation && data.interpretation.explanation_html) {
      $('#memory-interpretation').classList.remove('hidden');
      $('#interp-text').innerHTML = data.interpretation.explanation_html;
    }

    // 2. Render Results
    const count = data.results.length;
    $('#results-title').textContent = `${count} ${count === 1 ? 'memory' : 'memories'} retrieved`;
    const understood = data.interpretation || {};
    const constraints = [
      understood.file_type_constraint && `${understood.file_type_constraint.toUpperCase()} format`,
      understood.temporal_constraint && understood.temporal_constraint.label
    ].filter(Boolean);
    $('#results-subtitle').textContent = constraints.length
      ? `Filtered by ${constraints.join(' and ')}, then ranked by relevance`
      : 'Ranked by topic match, term coverage, title evidence, and phrase relevance';

    if (count === 0) {
      $('#results').innerHTML = `
        <div class="empty">
          <b>No matching memories surfaced.</b><br>
          Try a different fragment, remove constraints, or check if the target folder was indexed.
        </div>
      `;
    } else {
      $('#results').innerHTML = data.results.map(r => `
        <article class="result-card">
          <div class="result-top">
            <span class="rank-pill">${r.rank}</span>
            <span class="result-title">${esc(r.title)}</span>
            <span class="confidence-badge">${r.confidence_pct}% MEMORY MATCH</span>
          </div>

          <div class="meta-tags-row">
            <span class="meta-tag">${esc((r.file_type || 'file').toUpperCase())}</span>
            <span class="meta-tag page-tag">📄 PAGE ${r.matched_page} of ${r.page_count}</span>
            ${r.modified_date_str ? `
              <span class="meta-tag time-tag ${r.temporal_matched ? 'time-match' : ''}">
                📅 ${r.temporal_matched ? '★ ' : ''}${esc(r.modified_date_str)}
              </span>
            ` : ''}
            ${r.tags ? `<span class="meta-tag">🏷 ${esc(r.tags)}</span>` : ''}
          </div>

          <div class="result-path">${esc(r.file_path)}</div>

          ${r.passage_html ? `
            <div class="result-passage">${r.passage_html}</div>
          ` : ''}

          <div class="why-box">
            <span>🧠 Why selected:</span> ${esc(r.why_selected)}
          </div>

          <div class="result-actions">
            <button class="secondary small inspect-doc-btn" data-id="${r.id}">
              Inspect Passage 🔍
            </button>
            <button class="secondary small open-file-btn" data-path="${esc(r.file_path)}">
              Open Original ↗
            </button>
          </div>
        </article>
      `).join('');
    }

    // 3. Render Related Memories Graph
    if (data.related && data.related.length > 0) {
      $('#related-block').classList.remove('hidden');
      $('#related').innerHTML = data.related.map(rel => `
        <div class="related-item">
          <div class="related-item-info">
            <b>${esc(rel.title)}</b>
            <small>CONNECTED CONCEPTS: ${esc((rel.connectingConcepts || []).join(', ') || 'associative cluster')}</small>
          </div>
          <button class="secondary small open-file-btn" data-path="${esc(rel.filePath)}">
            Open Original ↗
          </button>
        </div>
      `).join('');
    }
  } catch (err) {
    $('#results-title').textContent = 'Search failed';
    $('#results').innerHTML = `<div class="empty">Error: ${esc(err.message)}</div>`;
  } finally {
    isBusy = false;
    $('#search-btn').disabled = false;
    $('#search-btn').textContent = 'Retrieve ↗';
  }
}

$('#search-btn').addEventListener('click', executeSearch);
$('#query').addEventListener('keydown', e => {
  if (e.key === 'Enter') executeSearch();
});

// Suggestions chips
$$('#suggestions button').forEach(btn => {
  btn.addEventListener('click', () => {
    $('#query').value = btn.dataset.query;
    executeSearch();
  });
});

// Filter event listeners
$('#filter-date').addEventListener('change', () => {
  if ($('#query').value.trim()) executeSearch();
});

$$('#format-pills .pill').forEach(pill => {
  pill.addEventListener('click', () => {
    $$('#format-pills .pill').forEach(p => p.classList.remove('active'));
    pill.classList.add('active');
    if ($('#query').value.trim()) executeSearch();
  });
});

// Clear Search
$('#clear-search-btn').addEventListener('click', () => {
  $('#query').value = '';
  $('#results-section').classList.add('hidden');
  $('#memory-interpretation').classList.add('hidden');
  $('#hero').classList.remove('hidden');
  $('#query').focus();
});

// ---------------------------------------------------------------------------
// Document Inspection Modal & Reader
// ---------------------------------------------------------------------------

async function openDocumentModal(docId) {
  try {
    const doc = await api(`/api/document?id=${docId}`);
    currentModalDoc = doc;
    currentModalPage = 1;

    $('#modal-doc-title').textContent = doc.title;
    $('#modal-doc-path').textContent = doc.file_path;
    $('#modal-type-badge').textContent = (doc.file_type || 'TXT').toUpperCase();
    $('#modal-method-badge').textContent = doc.extraction_method || 'Native Text';
    $('#modal-page-count-badge').textContent = `${doc.total_pages} ${doc.total_pages === 1 ? 'Page' : 'Pages'}`;

    $('#modal-detail-method').textContent = doc.extraction_method || 'Native Text';
    $('#modal-detail-size').textContent = formatBytes(doc.size_bytes);
    $('#modal-detail-modified').textContent = doc.modified_date_str || '-';

    $('#modal-edit-title').value = doc.title || '';
    $('#modal-edit-tags').value = doc.tags || '';
    $('#modal-edit-notes').value = doc.user_notes || '';

    // Render Page Switcher Pills
    const pillsContainer = $('#modal-page-pills');
    pillsContainer.innerHTML = '';
    for (let p = 1; p <= doc.total_pages; p++) {
      const btn = document.createElement('button');
      btn.className = `pill small ${p === 1 ? 'active' : ''}`;
      btn.textContent = `Page ${p}`;
      btn.addEventListener('click', () => switchModalPage(p));
      pillsContainer.appendChild(btn);
    }

    renderModalPageText();
    $('#doc-modal').classList.remove('hidden');
  } catch (err) {
    toast(`Could not load document: ${err.message}`);
  }
}

function switchModalPage(pageNum) {
  currentModalPage = pageNum;
  $$('#modal-page-pills .pill').forEach((p, idx) => {
    p.classList.toggle('active', idx + 1 === pageNum);
  });
  renderModalPageText();
}

function renderModalPageText() {
  if (!currentModalDoc || !currentModalDoc.pages) return;
  const pageData = currentModalDoc.pages[currentModalPage - 1];
  const textElem = $('#modal-extracted-text');
  textElem.textContent = pageData ? pageData.text : '(No text available for this page)';
}

$('#close-doc-modal').addEventListener('click', () => {
  $('#doc-modal').classList.add('hidden');
});

$('#doc-modal').addEventListener('click', e => {
  if (e.target.id === 'doc-modal') $('#doc-modal').classList.add('hidden');
});

$('#modal-open-original-btn').addEventListener('click', async () => {
  if (currentModalDoc && currentModalDoc.file_path) {
    try {
      await api('/api/open', 'POST', { path: currentModalDoc.file_path });
      toast('Opening file in default viewer...');
    } catch (err) {
      toast(err.message);
    }
  }
});

$('#modal-save-meta-btn').addEventListener('click', async () => {
  if (!currentModalDoc) return;
  const newTitle = $('#modal-edit-title').value.trim();
  const newTags = $('#modal-edit-tags').value.trim();
  const newNotes = $('#modal-edit-notes').value.trim();

  try {
    await api('/api/metadata', 'POST', {
      id: currentModalDoc.id,
      title: newTitle,
      tags: newTags,
      user_notes: newNotes
    });
    toast('Document metadata updated successfully!');
    currentModalDoc.title = newTitle;
    currentModalDoc.tags = newTags;
    currentModalDoc.user_notes = newNotes;
    $('#modal-doc-title').textContent = newTitle;
    if (currentView === 'library') loadLibraryTable();
  } catch (err) {
    toast(`Update failed: ${err.message}`);
  }
});

// Event delegation for opening files and inspecting documents
document.addEventListener('click', async e => {
  const openBtn = e.target.closest('.open-file-btn');
  if (openBtn) {
    try {
      await api('/api/open', 'POST', { path: openBtn.dataset.path });
      toast('Opening file in default viewer...');
    } catch (err) {
      toast(err.message);
    }
    return;
  }

  const inspectBtn = e.target.closest('.inspect-doc-btn');
  if (inspectBtn) {
    openDocumentModal(inspectBtn.dataset.id);
  }
});

// ---------------------------------------------------------------------------
// Library & Background Indexing
// ---------------------------------------------------------------------------

$('#index-btn').addEventListener('click', async () => {
  const folder = $('#folder').value.trim();
  if (!folder) return toast('Please specify a folder path.');

  try {
    $('#index-btn').disabled = true;
    $('#indexing-progress-card').classList.remove('hidden');
    $('#progress-status-text').textContent = 'Starting indexing job...';
    $('#progress-file-text').textContent = folder;
    $('#indexing-logs').textContent = 'Initializing engine...\n';

    await api('/api/index', 'POST', { folder });
    toast('Indexing started in background!');
    startIndexingPolling();
  } catch (err) {
    toast(`Indexing failed to start: ${err.message}`);
    $('#index-btn').disabled = false;
  }
});

$('#cancel-index-btn').addEventListener('click', async () => {
  try {
    await api('/api/index/cancel', 'POST', {});
    toast('Indexing cancellation requested.');
  } catch (err) {
    toast(err.message);
  }
});

function startIndexingPolling() {
  if (progressPollingInterval) clearInterval(progressPollingInterval);
  progressPollingInterval = setInterval(checkIndexingProgress, 1200);
}

async function checkIndexingProgress() {
  try {
    const data = await api('/api/index/progress');
    if (data.status === 'indexing') {
      $('#indexing-progress-card').classList.remove('hidden');
      $('#progress-status-text').textContent = 'Indexing in progress...';
      $('#progress-file-text').textContent = data.current_file || '';
      $('#progress-bar-fill').style.width = `${Math.max(15, data.percent || 20)}%`;
      $('#indexing-logs').textContent = (data.logs || []).join('\n');
      $('#indexing-logs').scrollTop = $('#indexing-logs').scrollHeight;
    } else if (data.status === 'completed') {
      $('#progress-status-text').textContent = 'Indexing completed successfully!';
      $('#progress-bar-fill').style.width = '100%';
      $('#indexing-logs').textContent = (data.logs || []).join('\n');
      $('#index-btn').disabled = false;
      if (progressPollingInterval) {
        clearInterval(progressPollingInterval);
        progressPollingInterval = null;
      }
      loadLibraryTable();
      refreshStatus();
    } else if (data.status === 'error') {
      $('#progress-status-text').textContent = `Error: ${data.error || 'Failed'}`;
      $('#index-btn').disabled = false;
      if (progressPollingInterval) {
        clearInterval(progressPollingInterval);
        progressPollingInterval = null;
      }
    }
  } catch {
    // Ignore transient poll errors
  }
}

async function loadLibraryTable() {
  const filter = $('#library-filter-input').value.trim();
  try {
    const data = await api(`/api/table?page=${libraryCurrentPage}&limit=15&filter=${encodeURIComponent(filter)}`);
    $('#library-table-count').textContent = `${data.total} documents found`;
    $('#nav-doc-count').textContent = data.total;

    const tbody = $('#library-table-body');
    if (data.rows.length === 0) {
      tbody.innerHTML = '<tr><td colspan="9" class="loading-td">No documents found in library.</td></tr>';
      return;
    }

    tbody.innerHTML = data.rows.map(r => `
      <tr>
        <td><code>${r.id}</code></td>
        <td><b>${esc(r.title)}</b></td>
        <td><span class="meta-tag">${esc((r.file_type || '').toUpperCase())}</span></td>
        <td>${r.page_count || 1}</td>
        <td>${formatBytes(r.size_bytes)}</td>
        <td>${esc(r.modified_date_str || '-')}</td>
        <td><small>${esc(r.extraction_method || 'Native Text')}</small></td>
        <td>${esc(r.tags || '-')}</td>
        <td>
          <button class="secondary small inspect-doc-btn" data-id="${r.id}">Inspect</button>
          <button class="secondary small open-file-btn" data-path="${esc(r.file_path)}">Open</button>
        </td>
      </tr>
    `).join('');

    $('#page-indicator').textContent = `Page ${data.page} of ${data.total_pages}`;
    $('#page-prev-btn').disabled = data.page <= 1;
    $('#page-next-btn').disabled = data.page >= data.total_pages;
  } catch (err) {
    $('#library-table-body').innerHTML = `<tr><td colspan="9" class="loading-td">Error: ${esc(err.message)}</td></tr>`;
  }
}

$('#library-filter-input').addEventListener('input', () => {
  libraryCurrentPage = 1;
  loadLibraryTable();
});

$('#page-prev-btn').addEventListener('click', () => {
  if (libraryCurrentPage > 1) {
    libraryCurrentPage--;
    loadLibraryTable();
  }
});

$('#page-next-btn').addEventListener('click', () => {
  libraryCurrentPage++;
  loadLibraryTable();
});

// ---------------------------------------------------------------------------
// Developer Console & Database Inspector
// ---------------------------------------------------------------------------

async function loadDevStats() {
  try {
    const stats = await api('/api/stats');
    $('#stat-docs').textContent = (stats.documents || 0).toLocaleString();
    $('#stat-db-size').textContent = formatBytes(stats.total_size_bytes);
    $('#stat-db-path').textContent = stats.database || 'library.db';
    $('#stat-terms').textContent = (stats.vocabulary || 0).toLocaleString();
    $('#stat-postings').textContent = (stats.postings || 0).toLocaleString();
    $('#stat-concepts').textContent = (stats.graph_concepts || 0).toLocaleString();
    $('#stat-edges').textContent = (stats.graph_edges || 0).toLocaleString();
    const semantic = await api('/api/semantic/status');
    const semanticCount = (stats.semantic_vectors || 0).toLocaleString();
    const progress = semantic.status === 'indexing'
      ? `Building ${semantic.processed}/${semantic.total} local AI vectors…`
      : `${semanticCount} vectors · ${semantic.model}`;
    $('#semantic-status').textContent = semantic.error ? `Local AI unavailable: ${semantic.error}` : progress;
    $('#build-semantic-btn').disabled = semantic.status === 'indexing';
  } catch (err) {
    toast(`Could not load developer statistics: ${err.message}`);
  }
}

$('#build-semantic-btn').addEventListener('click', async () => {
  try {
    const result = await api('/api/semantic/index', 'POST', {});
    toast(result.message);
    $('#build-semantic-btn').disabled = true;
    $('#semantic-status').textContent = 'Building local AI vectors…';
  } catch (err) {
    toast(err.message);
    loadDevStats();
  }
});

$('#sync-spotlight-btn').addEventListener('click', async () => {
  const button = $('#sync-spotlight-btn');
  try {
    button.disabled = true;
    button.textContent = 'Syncing…';
    const result = await api('/api/spotlight/sync', 'POST', {});
    toast(result.message || 'AFTERIMAGE documents were published to Spotlight.');
  } catch (err) {
    toast(err.message);
  } finally {
    button.disabled = false;
    button.textContent = 'Sync to Spotlight';
  }
});

$$('.preset-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    $('#sql-input').value = btn.dataset.sql;
    executeSqlQuery();
  });
});

$('#run-sql-btn').addEventListener('click', executeSqlQuery);

async function executeSqlQuery() {
  const sql = $('#sql-input').value.trim();
  if (!sql) return;

  const statusElem = $('#sql-status');
  statusElem.textContent = 'Executing query...';
  $('#sql-results-wrap').classList.remove('hidden');

  try {
    const data = await api('/api/sql', 'POST', { sql });
    statusElem.textContent = `Returned ${data.row_count} row${data.row_count === 1 ? '' : 's'}${data.truncated ? ' (truncated to 100)' : ''}`;

    const headElem = $('#sql-table-head');
    headElem.innerHTML = `<tr>${data.columns.map(c => `<th>${esc(c.toUpperCase())}</th>`).join('')}</tr>`;

    const bodyElem = $('#sql-table-body');
    if (data.rows.length === 0) {
      bodyElem.innerHTML = `<tr><td colspan="${data.columns.length || 1}" class="loading-td">Query returned 0 rows.</td></tr>`;
    } else {
      bodyElem.innerHTML = data.rows.map(row => `
        <tr>
          ${row.map(val => `<td>${esc(val === null ? 'NULL' : String(val))}</td>`).join('')}
        </tr>
      `).join('');
    }
  } catch (err) {
    statusElem.textContent = `Error: ${err.message}`;
    $('#sql-table-head').innerHTML = '';
    $('#sql-table-body').innerHTML = `<tr><td class="loading-td text-danger">Query Error: ${esc(err.message)}</td></tr>`;
  }
}

// ---------------------------------------------------------------------------
// Global Status Refresh & Shortcuts
// ---------------------------------------------------------------------------

async function refreshStatus() {
  try {
    const s = await api('/api/status');
    const shortLabel = s.database_exists
      ? `${formatBytes(s.database_bytes)} · Local SQLite`
      : 'No database yet';
    $('#db-status-short').textContent = shortLabel;

    const stats = await api('/api/stats');
    $('#nav-doc-count').textContent = stats.documents || 0;
  } catch {
    // Ignore
  }
}

// Help Modal
$('#help-btn').onclick = () => $('#help-modal').classList.remove('hidden');
$('#close-help').onclick = $('#got-it').onclick = () => $('#help-modal').classList.add('hidden');
$('#help-modal').addEventListener('click', e => {
  if (e.target.id === 'help-modal') $('#help-modal').classList.add('hidden');
});

// Global Keyboard Shortcuts
document.addEventListener('keydown', e => {
  if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
    e.preventDefault();
    setView('search');
    $('#query').focus();
    $('#query').select();
  }
  if (e.key === 'Escape') {
    $('#help-modal').classList.add('hidden');
    $('#doc-modal').classList.add('hidden');
  }
});

// Initialization. The native launcher can open a specific app view.
const requestedView = new URLSearchParams(window.location.search).get('view');
if (requestedView && ['search', 'library', 'developer'].includes(requestedView)) setView(requestedView);
refreshStatus();
