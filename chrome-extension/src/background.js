const DEFAULT_SETTINGS = {
  deviceIp: "",
  endpoint: "",
  deviceKey: "",
  autoPost: true
};

chrome.runtime.onInstalled.addListener(async () => {
  const existing = await chrome.storage.local.get(Object.keys(DEFAULT_SETTINGS));
  await chrome.storage.local.set({ ...DEFAULT_SETTINGS, ...existing });
});

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === "TOKKANI_USAGE_READING") {
    handleUsageReading(message.payload)
      .then((result) => sendResponse(result))
      .catch((error) => sendResponse({ ok: false, error: error.message }));
    return true;
  }

  if (message?.type === "TOKKANI_GET_STATE") {
    getState()
      .then((state) => sendResponse({ ok: true, state }))
      .catch((error) => sendResponse({ ok: false, error: error.message }));
    return true;
  }

  if (message?.type === "TOKKANI_SAVE_SETTINGS") {
    saveSettings(message.payload)
      .then((state) => sendResponse({ ok: true, state }))
      .catch((error) => sendResponse({ ok: false, error: error.message }));
    return true;
  }

  if (message?.type === "TOKKANI_SYNC_DEVICE") {
    syncDevice()
      .then((result) => sendResponse({ ok: true, result }))
      .catch((error) => sendResponse({ ok: false, error: error.message }));
    return true;
  }

  if (message?.type === "TOKKANI_OPEN_USAGE_PAGE") {
    const url = message.provider === "claude"
      ? "https://claude.ai/settings/usage"
      : "https://chatgpt.com/codex/cloud/settings/analytics";
    chrome.tabs.create({ url });
    sendResponse({ ok: true });
    return false;
  }

  return false;
});

async function getState() {
  const { latest = {}, lastPosts = {} } = await chrome.storage.local.get(["latest", "lastPosts"]);
  const settings = await chrome.storage.local.get(Object.keys(DEFAULT_SETTINGS));
  return {
    settings: { ...DEFAULT_SETTINGS, ...settings },
    latest,
    lastPosts
  };
}

async function saveSettings(payload = {}) {
  const deviceIp = String(payload.deviceIp || "").trim().replace(/^https?:\/\//, "").replace(/\/.*$/, "");
  const settings = {
    deviceIp,
    endpoint: deviceIp ? "http://" + deviceIp + "/usage" : "",
    deviceKey: String(payload.deviceKey || ""),
    autoPost: payload.autoPost !== false
  };
  await chrome.storage.local.set(settings);
  return getState();
}

async function handleUsageReading(payload) {
  const reading = normalizeReading(payload);
  if (!reading) {
    return { ok: false, error: "No usage values found." };
  }

  const { latest = {} } = await chrome.storage.local.get("latest");
  latest[reading.provider] = reading;
  await chrome.storage.local.set({ latest });
  await syncLatestReadings({ providers: [reading.provider] });
  return { ok: true, reading };
}

async function syncDevice() {
  const settings = { ...DEFAULT_SETTINGS, ...(await chrome.storage.local.get(Object.keys(DEFAULT_SETTINGS))) };
  if (!settings.endpoint) {
    throw new Error("Configure the ESP32 IP address first.");
  }

  const statusUrl = settings.endpoint.replace(/\/usage$/, "/status");
  const response = await fetch(statusUrl);
  if (!response.ok) {
    throw new Error("ESP32 returned HTTP " + response.status);
  }

  const refresh = await refreshOpenUsageTabs();
  if (refresh.refreshedProviders.length === 0) {
    throw new Error("Open the Claude or Codex usage page first.");
  }

  const sync = await syncLatestReadings({
    ignoreAutoPost: true,
    forceWakeUp: true,
    failOnPostError: true,
    providers: refresh.refreshedProviders
  });
  return { refresh, sync };
}

async function refreshOpenUsageTabs() {
  const result = { refreshedProviders: [], errors: [] };
  const tabSpecs = [
    { provider: "claude", url: "https://claude.ai/*" },
    { provider: "codex", url: "https://chatgpt.com/*" }
  ];
  const { latest = {} } = await chrome.storage.local.get("latest");

  for (const spec of tabSpecs) {
    const tabs = await chrome.tabs.query({ url: spec.url });
    for (const tab of tabs) {
      try {
        const response = await sendSampleNowToTab(tab.id);
        if (!response?.ok || !response.reading) {
          continue;
        }
        latest[spec.provider] = normalizeReading(response.reading);
        result.refreshedProviders.push(spec.provider);
        break;
      } catch (error) {
        result.errors.push(spec.provider + ": " + error.message);
      }
    }
  }

  if (result.refreshedProviders.length > 0) {
    await chrome.storage.local.set({ latest });
  }
  return result;
}

function sendSampleNowToTab(tabId) {
  return new Promise((resolve, reject) => {
    chrome.tabs.sendMessage(tabId, { type: "TOKKANI_SAMPLE_NOW" }, (response) => {
      if (chrome.runtime.lastError) {
        reject(new Error(chrome.runtime.lastError.message));
        return;
      }
      resolve(response);
    });
  });
}

async function syncLatestReadings(options = {}) {
  const settings = { ...DEFAULT_SETTINGS, ...(await chrome.storage.local.get(Object.keys(DEFAULT_SETTINGS))) };
  if ((!settings.autoPost && !options.ignoreAutoPost) || !settings.endpoint) {
    return { posted: false, postedProviders: [] };
  }

  const { latest = {}, lastPosts = {} } = await chrome.storage.local.get(["latest", "lastPosts"]);
  const requestedProviders = Array.isArray(options.providers) ? options.providers : ["claude", "codex"];
  const providers = ["claude", "codex"].filter((provider) => requestedProviders.includes(provider) && latest[provider]);
  const failures = [];

  for (const provider of providers) {
    try {
      await postReading(settings, latest[provider], options.forceWakeUp === true);
      lastPosts[provider] = { ok: true, at: new Date().toISOString() };
    } catch (error) {
      const failure = { ok: false, at: new Date().toISOString(), error: error.message };
      lastPosts[provider] = failure;
      failures.push(provider + ": " + error.message);
    }
  }

  await chrome.storage.local.set({ lastPosts });
  if (options.failOnPostError && failures.length > 0) {
    throw new Error(failures.join(" | "));
  }
  return { posted: providers.length > 0, postedProviders: providers };
}

function normalizeReading(payload) {
  if (payload?.provider !== "claude" && payload?.provider !== "codex") {
    return null;
  }

  const reading = {
    provider: payload.provider,
    sessionUsedPct: toPercentage(payload.sessionUsedPct),
    fiveHourUsedPct: toPercentage(payload.fiveHourUsedPct),
    weeklyUsedPct: toPercentage(payload.weeklyUsedPct),
    resetText: String(payload.resetText || "").slice(0, 160),
    capturedAt: payload.capturedAt || new Date().toISOString()
  };
  if (reading.sessionUsedPct === null && reading.fiveHourUsedPct === null && reading.weeklyUsedPct === null) {
    return null;
  }
  reading.sessionUsedPct ??= -1;
  reading.fiveHourUsedPct ??= -1;
  reading.weeklyUsedPct ??= -1;
  return reading;
}

function toPercentage(value) {
  if (value === undefined || value === null || value === "") {
    return null;
  }
  const number = Number(String(value).replace("%", "").trim());
  return Number.isFinite(number) && number >= 0 ? Math.max(0, Math.min(100, Math.round(number))) : null;
}

async function postReading(settings, reading, forceWakeUp) {
  const headers = { "Content-Type": "application/json" };
  if (settings.deviceKey) {
    headers["X-Tokkani-Key"] = settings.deviceKey;
  }
  const response = await fetch(settings.endpoint, {
    method: "POST",
    headers,
    body: JSON.stringify({ ...reading, forceWakeUp })
  });
  if (!response.ok && response.status !== 204) {
    throw new Error("Device returned HTTP " + response.status);
  }
}
