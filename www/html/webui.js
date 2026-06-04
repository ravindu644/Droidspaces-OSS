'use strict';

const $ = (id) => document.getElementById(id);

function pretty(value) {
  if (typeof value === 'string') return value;
  return JSON.stringify(value, null, 2);
}

async function fetchText(path, options = {}) {
  const started = performance.now();
  const response = await fetch(path, options);
  const elapsed = Math.round(performance.now() - started);
  const text = await response.text();
  return {
    path,
    method: options.method || 'GET',
    ok: response.ok,
    status: response.status,
    statusText: response.statusText,
    contentType: response.headers.get('content-type'),
    elapsedMs: elapsed,
    body: text,
  };
}

async function fetchJsonish(path, options = {}) {
  const result = await fetchText(path, options);
  const contentType = result.contentType || '';
  if (contentType.includes('application/json') && result.body.length) {
    try {
      result.parsed = JSON.parse(result.body);
    } catch (e) {
      result.parseError = String(e);
    }
  }
  return result;
}

function renderResult(target, result) {
  const copy = { ...result };
  if (copy.body && copy.body.length > 4096) {
    copy.body = copy.body.slice(0, 4096) + '\n... truncated ...';
  }
  target.textContent = pretty(copy);
}

async function runStatic(path) {
  const target = $('static-result');
  target.textContent = `Loading ${path} ...`;
  try {
    const result = await fetchText(path);
    if (result.body.length > 900) result.body = result.body.slice(0, 900) + '\n... truncated ...';
    renderResult(target, result);
  } catch (e) {
    target.textContent = `FAILED: ${e}`;
  }
}

async function runApi(path) {
  const target = $('api-result');
  target.textContent = `Loading ${path} ...`;
  try {
    renderResult(target, await fetchJsonish(path));
  } catch (e) {
    target.textContent = `FAILED: ${e}`;
  }
}

function containerName(item) {
  if (Array.isArray(item.Names) && item.Names.length) return item.Names[0].replace(/^\//, '');
  return item.Name || item.Id || item.ID || '(unnamed)';
}

function containerState(item) {
  const state = item.State || item.Status || '';
  if (/running/i.test(state)) return ['running', 'status-running'];
  if (/exited|stopped|created/i.test(state)) return ['stopped', 'status-stopped'];
  return [state || 'unknown', 'status-unknown'];
}

async function refreshContainers() {
  const box = $('containers');
  box.textContent = 'Loading containers ...';
  try {
    const result = await fetchJsonish('/containers/json?all=1');
    if (!result.ok) {
      box.textContent = `API returned ${result.status} ${result.statusText}`;
      return;
    }
    const list = Array.isArray(result.parsed) ? result.parsed : [];
    if (!list.length) {
      box.textContent = 'No containers returned by /containers/json?all=1.';
      return;
    }
    box.innerHTML = '';
    for (const item of list) {
      const name = containerName(item);
      const [state, cls] = containerState(item);
      const id = item.Id || item.ID || '';
      const image = item.Image || '';
      const node = document.createElement('article');
      node.className = 'container-card';
      node.innerHTML = `
        <h3></h3>
        <div class="${cls}"></div>
        <p></p>
        <div class="button-row">
          <button data-container-action="inspect">inspect</button>
          <button data-container-action="start">start</button>
          <button data-container-action="stop">stop</button>
          <button data-container-action="restart">restart</button>
        </div>
      `;
      node.querySelector('h3').textContent = name;
      node.querySelector(`.${cls}`).textContent = state;
      node.querySelector('p').textContent = [image, id ? `id=${String(id).slice(0, 12)}` : ''].filter(Boolean).join(' · ');
      node.querySelectorAll('[data-container-action]').forEach((button) => {
        button.addEventListener('click', async () => {
          const action = button.getAttribute('data-container-action');
          if (action === 'inspect') {
            renderResult($('lifecycle-result'), await fetchJsonish(`/containers/${encodeURIComponent(name)}/json`));
          } else {
            renderResult($('lifecycle-result'), await fetchJsonish(`/containers/${encodeURIComponent(name)}/${action}`, { method: 'POST' }));
            await refreshContainers();
          }
        });
      });
      box.appendChild(node);
    }
  } catch (e) {
    box.textContent = `FAILED: ${e}`;
  }
}

async function runAll() {
  await runStatic('/assets/sample.json');
  await runApi('/_ping');
  await refreshContainers();
}

function init() {
  $('origin-label').textContent = `Page origin: ${location.origin}. API calls are relative to this origin.`;

  document.querySelectorAll('[data-static]').forEach((button) => {
    button.addEventListener('click', () => runStatic(button.getAttribute('data-static')));
  });

  document.querySelectorAll('[data-api]').forEach((button) => {
    button.addEventListener('click', () => runApi(button.getAttribute('data-api')));
  });

  $('head-index').addEventListener('click', async () => {
    const target = $('static-result');
    try {
      const response = await fetch('/', { method: 'HEAD' });
      renderResult(target, {
        path: '/',
        method: 'HEAD',
        ok: response.ok,
        status: response.status,
        statusText: response.statusText,
        contentType: response.headers.get('content-type'),
        contentLength: response.headers.get('content-length'),
      });
    } catch (e) {
      target.textContent = `FAILED: ${e}`;
    }
  });

  $('refresh-containers').addEventListener('click', refreshContainers);
  $('run-all').addEventListener('click', runAll);

  $('lifecycle-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    const submitter = event.submitter;
    const action = submitter ? submitter.value : 'inspect';
    const ref = $('container-ref').value.trim();
    const target = $('lifecycle-result');
    if (!ref) {
      target.textContent = 'Enter a container name or ID first.';
      return;
    }
    try {
      const path = `/containers/${encodeURIComponent(ref)}/${action}`;
      renderResult(target, await fetchJsonish(path, { method: 'POST' }));
      await refreshContainers();
    } catch (e) {
      target.textContent = `FAILED: ${e}`;
    }
  });

  $('load-events').addEventListener('click', async () => {
    const target = $('events-result');
    try {
      renderResult(target, await fetchJsonish('/events'));
    } catch (e) {
      target.textContent = `FAILED: ${e}`;
    }
  });
}

document.addEventListener('DOMContentLoaded', init);
