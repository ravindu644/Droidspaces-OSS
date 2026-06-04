(function () {
  "use strict";

  const state = {
    containers: [],
    version: null,
    info: null,
    busyRefs: new Set(),
    lastError: ""
  };

  const $ = (id) => document.getElementById(id);

  const els = {
    badge: $("daemon-badge"),
    badgeText: $("daemon-badge-text"),
    consolePing: $("console-ping"),
    consoleVersion: $("console-version"),
    consoleContainers: $("console-containers"),
    statTotal: $("stat-total"),
    statRunning: $("stat-running"),
    statStopped: $("stat-stopped"),
    refreshButton: $("refresh-button"),
    recheckButton: $("recheck-button"),
    showAll: $("show-all"),
    grid: $("containers-grid"),
    empty: $("empty-state"),
    alert: $("global-alert"),
    details: $("details"),
    detailsTitle: $("details-title"),
    detailsSummary: $("details-summary"),
    detailsJson: $("details-json"),
    diagnostics: $("daemon-diagnostics"),
    manualForm: $("manual-form"),
    manualPath: $("manual-path"),
    manualOutput: $("manual-output"),
    themeToggle: $("theme-toggle")
  };

  function setText(el, value) {
    if (el) el.textContent = value;
  }

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }

  function shortId(value) {
    const text = String(value || "");
    return text.length > 12 ? text.slice(0, 12) : text || "—";
  }

  function normalizeName(container) {
    if (!container) return "unknown";
    if (Array.isArray(container.Names) && container.Names.length > 0) {
      return String(container.Names[0]).replace(/^\/+/, "") || "unknown";
    }
    if (typeof container.Names === "string") return container.Names.replace(/^\/+/, "") || "unknown";
    if (container.Name) return String(container.Name).replace(/^\/+/, "") || "unknown";
    if (container.Id) return shortId(container.Id);
    return "unknown";
  }

  function containerRef(container) {
    return normalizeName(container) || container.Id || "";
  }

  function normalizeState(container) {
    const raw = String(container.State || container.Status || "unknown").toLowerCase();
    if (raw.includes("running") || raw === "running") return "running";
    if (raw.includes("exit") || raw.includes("stop") || raw === "created") return "stopped";
    return raw.replace(/[^a-z0-9_-]+/g, "-") || "unknown";
  }

  function normalizeImage(container) {
    return container.Image || container.ImageID || container.ImageRef || "—";
  }

  function normalizeStatus(container) {
    return container.Status || container.State || "unknown";
  }

  function summarizePorts(container) {
    const ports = Array.isArray(container.Ports) ? container.Ports : [];
    if (!ports.length) return "—";
    return ports.slice(0, 3).map((port) => {
      const type = port.Type || port.Proto || "tcp";
      const privatePort = port.PrivatePort || port.ContainerPort || "?";
      const publicPort = port.PublicPort || port.HostPort;
      if (publicPort) return `${publicPort}->${privatePort}/${type}`;
      return `${privatePort}/${type}`;
    }).join(", ") + (ports.length > 3 ? " …" : "");
  }

  function setDaemonStatus(kind, message) {
    els.badge.classList.remove("online", "offline");
    if (kind) els.badge.classList.add(kind);
    setText(els.badgeText, message);
  }

  function setAlert(message, ok) {
    if (!message) {
      els.alert.hidden = true;
      els.alert.textContent = "";
      els.alert.classList.remove("ok");
      return;
    }
    els.alert.hidden = false;
    els.alert.textContent = message;
    els.alert.classList.toggle("ok", Boolean(ok));
  }

  async function apiFetch(path, options) {
    const response = await fetch(path, Object.assign({ cache: "no-store" }, options || {}));
    const contentType = response.headers.get("content-type") || "";
    let body;
    if (contentType.includes("application/json")) {
      body = await response.json().catch(() => null);
    } else {
      body = await response.text().catch(() => "");
    }

    if (!response.ok && response.status !== 304) {
      const message = body && typeof body === "object" && body.message
        ? body.message
        : `${response.status} ${response.statusText}`;
      const err = new Error(message);
      err.status = response.status;
      err.body = body;
      throw err;
    }

    return { response, body };
  }

  async function checkDaemon() {
    setDaemonStatus("", "checking local daemon");
    setText(els.consolePing, "pending");
    setText(els.consoleVersion, "pending");

    try {
      const ping = await apiFetch("/_ping");
      setText(els.consolePing, String(ping.body || "OK"));

      const version = await apiFetch("/version");
      state.version = version.body;
      const versionText = version.body && version.body.Version
        ? `${version.body.Version} · API ${version.body.ApiVersion || "?"}`
        : "version response received";
      setText(els.consoleVersion, versionText);
      setDaemonStatus("online", "local daemon online");
      return true;
    } catch (err) {
      state.lastError = err.message;
      setText(els.consolePing, `failed: ${err.message}`);
      setText(els.consoleVersion, "skipped");
      setDaemonStatus("offline", "local daemon unavailable");
      return false;
    }
  }

  async function loadInfo() {
    try {
      const info = await apiFetch("/info");
      state.info = info.body;
    } catch (err) {
      state.info = null;
    }
  }

  async function loadContainers() {
    const all = els.showAll.checked ? "1" : "0";
    const result = await apiFetch(`/containers/json?all=${all}`);
    state.containers = Array.isArray(result.body) ? result.body : [];
    setText(els.consoleContainers, `${state.containers.length} container(s)`);
  }

  function renderStats() {
    const total = state.containers.length;
    const running = state.containers.filter((c) => normalizeState(c) === "running").length;
    const stopped = total - running;
    setText(els.statTotal, String(total));
    setText(els.statRunning, String(running));
    setText(els.statStopped, String(stopped));
  }

  function actionButton(action, label, container, cssClass) {
    const ref = containerRef(container);
    const disabled = state.busyRefs.has(`${ref}:${action}`) ? " disabled" : "";
    const cls = cssClass ? ` ${cssClass}` : "";
    return `<button class="btn${cls}" type="button" data-action="${action}" data-ref="${escapeHtml(ref)}"${disabled}>${label}</button>`;
  }

  function renderContainerCard(container) {
    const name = normalizeName(container);
    const stateClass = normalizeState(container);
    const running = stateClass === "running";
    const id = shortId(container.Id || container.ID || container.Name || name);
    const image = normalizeImage(container);
    const status = normalizeStatus(container);
    const ports = summarizePorts(container);

    return `
      <article class="container-card" data-ref="${escapeHtml(containerRef(container))}">
        <div class="card-top">
          <div>
            <div class="container-name">${escapeHtml(name)}</div>
            <div class="container-id">${escapeHtml(id)}</div>
          </div>
          <div class="status-pill ${escapeHtml(stateClass)}">${escapeHtml(stateClass)}</div>
        </div>
        <div class="card-meta">
          <div class="meta-item"><div class="meta-label">Image</div><div class="meta-value" title="${escapeHtml(image)}">${escapeHtml(image)}</div></div>
          <div class="meta-item"><div class="meta-label">Status</div><div class="meta-value" title="${escapeHtml(status)}">${escapeHtml(status)}</div></div>
          <div class="meta-item"><div class="meta-label">Ports</div><div class="meta-value" title="${escapeHtml(ports)}">${escapeHtml(ports)}</div></div>
          <div class="meta-item"><div class="meta-label">Created</div><div class="meta-value">${escapeHtml(formatCreated(container.Created))}</div></div>
        </div>
        <div class="card-actions">
          ${running ? actionButton("stop", "Stop", container, "btn-danger") : actionButton("start", "Start", container, "btn-primary")}
          ${actionButton("restart", "Restart", container, "btn-ghost")}
          ${actionButton("inspect", "Inspect", container, "btn-ghost")}
        </div>
      </article>`;
  }

  function formatCreated(value) {
    if (!value) return "—";
    const numeric = Number(value);
    if (!Number.isFinite(numeric) || numeric <= 0) return "—";
    const date = new Date(numeric * 1000);
    if (Number.isNaN(date.getTime())) return "—";
    return date.toLocaleString();
  }

  function renderContainers() {
    els.grid.innerHTML = state.containers.map(renderContainerCard).join("");
    els.empty.hidden = state.containers.length !== 0;
    renderStats();
  }

  function renderDiagnostics(online) {
    const version = state.version || {};
    const info = state.info || {};
    const items = [];
    items.push(`API status: ${online ? "online" : "offline"}`);
    if (version.Version) items.push(`Engine version: ${version.Version}`);
    if (version.ApiVersion) items.push(`API version: ${version.ApiVersion}`);
    if (version.Arch) items.push(`Architecture: ${version.Arch}`);
    if (info.Containers !== undefined) items.push(`Info containers: ${info.Containers}`);
    if (info.ContainersRunning !== undefined) items.push(`Info running: ${info.ContainersRunning}`);
    if (!items.length) items.push("No diagnostic information available.");
    els.diagnostics.innerHTML = items.map((item) => `<li>${escapeHtml(item)}</li>`).join("");
  }

  async function refreshDashboard() {
    setAlert("");
    els.refreshButton.disabled = true;
    try {
      const online = await checkDaemon();
      if (online) {
        await loadInfo();
        await loadContainers();
        renderContainers();
        renderDiagnostics(true);
      } else {
        renderDiagnostics(false);
      }
    } catch (err) {
      setText(els.consoleContainers, `failed: ${err.message}`);
      setAlert(`Dashboard refresh failed: ${err.message}`);
      renderDiagnostics(false);
    } finally {
      els.refreshButton.disabled = false;
    }
  }

  async function lifecycleAction(ref, action) {
    const key = `${ref}:${action}`;
    state.busyRefs.add(key);
    renderContainers();
    try {
      await apiFetch(`/containers/${encodeURIComponent(ref)}/${action}`, { method: "POST" });
      setAlert(`${action} sent to ${ref}`, true);
      await loadContainers();
      renderContainers();
    } catch (err) {
      if (err.status === 304) {
        setAlert(`${ref} already in requested state`, true);
        await loadContainers();
        renderContainers();
      } else {
        setAlert(`${action} failed for ${ref}: ${err.message}`);
      }
    } finally {
      state.busyRefs.delete(key);
      renderContainers();
    }
  }

  function detailValue(value) {
    if (value === undefined || value === null || value === "") return "—";
    if (typeof value === "boolean") return value ? "true" : "false";
    return String(value);
  }

  function summaryRow(label, value) {
    return `<div><dt>${escapeHtml(label)}</dt><dd>${escapeHtml(detailValue(value))}</dd></div>`;
  }

  async function inspectContainer(ref) {
    setAlert("");
    try {
      const result = await apiFetch(`/containers/${encodeURIComponent(ref)}/json`);
      const data = result.body || {};
      els.details.hidden = false;
      setText(els.detailsTitle, `${ref} details`);
      const stateData = data.State || {};
      const config = data.Config || {};
      const network = data.NetworkSettings || {};
      els.detailsSummary.innerHTML = [
        summaryRow("Name", (data.Name || ref).replace(/^\/+/, "")),
        summaryRow("ID", shortId(data.Id || data.ID || ref)),
        summaryRow("Image", config.Image || data.Image || data.ImageName),
        summaryRow("Running", stateData.Running),
        summaryRow("PID", stateData.Pid || data.Pid),
        summaryRow("Status", stateData.Status || data.Status),
        summaryRow("Hostname", config.Hostname || data.Hostname),
        summaryRow("IP address", network.IPAddress || data.IPAddress)
      ].join("");
      setText(els.detailsJson, JSON.stringify(data, null, 2));
      location.hash = "details";
    } catch (err) {
      setAlert(`inspect failed for ${ref}: ${err.message}`);
    }
  }

  async function manualGet(path) {
    let requestPath = path.trim();
    if (!requestPath.startsWith("/")) requestPath = `/${requestPath}`;
    els.manualOutput.textContent = "Loading…";
    try {
      const result = await apiFetch(requestPath);
      if (typeof result.body === "string") {
        els.manualOutput.textContent = result.body || "<empty response>";
      } else {
        els.manualOutput.textContent = JSON.stringify(result.body, null, 2);
      }
    } catch (err) {
      els.manualOutput.textContent = `ERROR: ${err.message}`;
    }
  }

  function setupThemeToggle() {
    const themeToggle = els.themeToggle;
    const sunIcon = themeToggle.querySelector(".sun-icon");
    const moonIcon = themeToggle.querySelector(".moon-icon");
    const mediaQuery = window.matchMedia("(prefers-color-scheme: light)");

    function updateIcons(theme) {
      if (theme === "light") {
        sunIcon.style.display = "none";
        moonIcon.style.display = "block";
      } else {
        sunIcon.style.display = "block";
        moonIcon.style.display = "none";
      }
    }

    function getEffectiveTheme() {
      const saved = localStorage.getItem("theme");
      if (saved) return saved;
      return mediaQuery.matches ? "light" : "dark";
    }

    function applyTheme(theme) {
      document.documentElement.setAttribute("data-theme", theme);
      updateIcons(theme);
    }

    applyTheme(getEffectiveTheme());
    mediaQuery.addEventListener("change", (event) => {
      if (!localStorage.getItem("theme")) applyTheme(event.matches ? "light" : "dark");
    });
    themeToggle.addEventListener("click", () => {
      const next = getEffectiveTheme() === "dark" ? "light" : "dark";
      localStorage.setItem("theme", next);
      applyTheme(next);
    });
  }

  function bindEvents() {
    els.refreshButton.addEventListener("click", refreshDashboard);
    els.recheckButton.addEventListener("click", checkDaemon);
    els.showAll.addEventListener("change", refreshDashboard);
    els.grid.addEventListener("click", (event) => {
      const button = event.target.closest("button[data-action]");
      if (!button) return;
      const action = button.dataset.action;
      const ref = button.dataset.ref;
      if (!ref) return;
      if (action === "inspect") inspectContainer(ref);
      else lifecycleAction(ref, action);
    });
    els.manualForm.addEventListener("submit", (event) => {
      event.preventDefault();
      manualGet(els.manualPath.value);
    });
  }

  document.addEventListener("DOMContentLoaded", () => {
    setupThemeToggle();
    bindEvents();
    refreshDashboard();
  });
})();
