const readings = document.querySelector("#readings");
const lastPost = document.querySelector("#lastPost");
const deviceIp = document.querySelector("#deviceIp");
const deviceKey = document.querySelector("#deviceKey");
const autoPost = document.querySelector("#autoPost");

loadState();
window.setInterval(loadState, 5000);

document.querySelector("#save").addEventListener("click", () => {
  chrome.runtime.sendMessage({
    type: "TOKKANI_SAVE_SETTINGS",
    payload: {
      deviceIp: deviceIp.value,
      deviceKey: deviceKey.value,
      autoPost: autoPost.checked
    }
  }, showStateResponse);
});

document.querySelector("#sync").addEventListener("click", () => {
  lastPost.textContent = "Syncing ESP32...";
  chrome.runtime.sendMessage({ type: "TOKKANI_SYNC_DEVICE" }, (response) => {
    if (!response?.ok) {
      lastPost.textContent = response?.error || "Could not reach ESP32.";
      return;
    }
    const providers = response.result.sync.postedProviders || [];
    lastPost.textContent = providers.length ? "Sync successful: " + providers.join(", ") : "Connected, but no usage data to sync.";
    loadState();
  });
});

document.querySelector("#openClaude").addEventListener("click", () => {
  chrome.runtime.sendMessage({ type: "TOKKANI_OPEN_USAGE_PAGE", provider: "claude" });
});

document.querySelector("#openCodex").addEventListener("click", () => {
  chrome.runtime.sendMessage({ type: "TOKKANI_OPEN_USAGE_PAGE", provider: "codex" });
});

function loadState() {
  chrome.runtime.sendMessage({ type: "TOKKANI_GET_STATE" }, showStateResponse);
}

function showStateResponse(response) {
  if (!response?.ok) {
    lastPost.textContent = response?.error || "No state yet.";
    return;
  }
  const settings = response.state.settings || {};
  const latest = response.state.latest || {};
  deviceIp.value = settings.deviceIp || "";
  deviceKey.value = settings.deviceKey || "";
  autoPost.checked = settings.autoPost !== false;
  readings.innerHTML = renderGaugeValues(latest);

  const summary = ["claude", "codex"]
    .map((provider) => formatPost(provider, response.state.lastPosts?.[provider]))
    .filter(Boolean)
    .join(" | ");
  if (summary) {
    lastPost.textContent = summary;
  }
}

function renderGaugeValues(latest) {
  const claude = latest.claude || null;
  const codex = latest.codex || null;
  return [
    renderGaugeRow("C 5h", claude ? remainingFromUsed(claude.fiveHourUsedPct ?? claude.sessionUsedPct) : null, claude),
    renderGaugeRow("C 7d", claude ? remainingFromUsed(claude.weeklyUsedPct) : null, claude),
    renderGaugeRow("X 7d", codex ? normalizePercent(codex.weeklyUsedPct) : null, codex)
  ].join("");
}

function renderGaugeRow(label, remaining, reading) {
  return "<article class=\"gauge-row\"><strong>" + label + "</strong><span>" + formatPercent(remaining) + "</span><span>" + (reading ? "remaining" : "no data") + "</span></article>";
}

function formatPost(provider, post) {
  if (!post) {
    return "";
  }
  const time = new Date(post.at).toLocaleTimeString();
  return post.ok ? provider + " ok " + time : provider + " " + post.error;
}

function remainingFromUsed(value) {
  const number = normalizePercent(value);
  return number === null ? null : 100 - number;
}

function normalizePercent(value) {
  const number = Number(value);
  return Number.isFinite(number) && number >= 0 ? Math.max(0, Math.min(100, Math.round(number))) : null;
}

function formatPercent(value) {
  return value === null ? "--" : value + "%";
}
