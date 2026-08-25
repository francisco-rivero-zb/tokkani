const PROVIDER = location.hostname === "claude.ai" ? "claude" : "codex";
const SAMPLE_INTERVAL_MS = 5000;
let extensionContextValid = true;

sampleDomSoon();
window.setInterval(sampleDomSoon, SAMPLE_INTERVAL_MS);

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type !== "TOKKANI_SAMPLE_NOW") {
    return false;
  }

  const reading = extractReadingFromText(document.body?.innerText || "", PROVIDER);
  if (!reading) {
    sendResponse({ ok: false, error: "No usage values found on this page." });
    return false;
  }

  const payload = { ...reading, capturedAt: new Date().toISOString() };
  sendReading(payload);
  sendResponse({ ok: true, reading: payload });
  return false;
});

function sampleDomSoon() {
  window.setTimeout(() => {
    const reading = extractReadingFromText(document.body?.innerText || "", PROVIDER);
    if (reading) {
      sendReading(reading);
    }
  }, 250);
}

function sendReading(reading) {
  if (!extensionContextValid || !chrome.runtime?.id) {
    extensionContextValid = false;
    return;
  }
  try {
    chrome.runtime.sendMessage({
      type: "TOKKANI_USAGE_READING",
      payload: { ...reading, capturedAt: new Date().toISOString() }
    }, () => {
      if (chrome.runtime.lastError) {
        extensionContextValid = false;
      }
    });
  } catch {
    extensionContextValid = false;
  }
}

function extractReadingFromText(text, provider) {
  if (provider === "claude") {
    return extractClaudeReading(text);
  }
  if (provider === "codex") {
    return extractCodexReading(text);
  }
  return null;
}

function extractClaudeReading(text) {
  const lines = normalizedLines(text);
  const session = percentAfterLine(lines, /^(?:current session|sesión actual)$/i, 8, true);
  const weekly = percentAfterLine(lines, /^(?:all models|todos los modelos)$/i, 8, true);
  if (session === null && weekly === null) {
    return null;
  }
  return {
    provider: "claude",
    sessionUsedPct: session ?? -1,
    fiveHourUsedPct: session ?? -1,
    weeklyUsedPct: weekly ?? -1,
    resetText: lines.find((line) => /^(?:resets|se reinicia)\b/i.test(line)) || ""
  };
}

function extractCodexReading(text) {
  const normalized = text.replace(/\s+/g, " ").trim();
  if (!/(?:usage limits|límites de uso)/i.test(normalized)) {
    return null;
  }

  const elementReading = extractCodexFromUsageLimitElement();
  if (elementReading !== null) {
    return codexReading(elementReading, "");
  }

  const lines = normalizedLines(text);
  const weekly = percentAfterLine(lines, /^(?:weekly|semanal|weekly limits|límite de uso semanal)$/i, 10, false)
    ?? firstRemainingPercent(lines);
  if (weekly === null) {
    return null;
  }
  const resetText = lines.find((line) => /^(?:resets|se reinicia)\b/i.test(line)) || "";
  return codexReading(weekly, resetText);
}

function extractCodexFromUsageLimitElement() {
  const label = [...document.querySelectorAll("div")].find((element) =>
    element.children.length === 0 &&
    /^(?:límite de uso )?(?:semanal|weekly)(?: usage limit)?$/i.test(cleanText(element.textContent))
  );
  if (!label) {
    return null;
  }
  const header = label.parentElement;
  const block = header?.parentElement;
  const percentageText = [...(header?.children || [])]
    .map((element) => cleanText(element.textContent))
    .find((value) => parseRemainingPercent(value) !== null);
  if (percentageText) {
    return parseRemainingPercent(percentageText);
  }
  const progress = [...(block?.querySelectorAll("div[style]") || [])]
    .find((element) => /\bwidth\s*:\s*\d+(?:\.\d+)?%/i.test(element.getAttribute("style") || ""));
  const match = progress?.getAttribute("style")?.match(/\bwidth\s*:\s*(\d{1,3}(?:\.\d+)?)%/i);
  return match ? Number(match[1]) : null;
}

function codexReading(weeklyUsedPct, resetText) {
  return {
    provider: "codex",
    sessionUsedPct: -1,
    fiveHourUsedPct: -1,
    weeklyUsedPct,
    resetText
  };
}

function normalizedLines(text) {
  return text.split(/\r?\n/).map(cleanText).filter(Boolean);
}

function cleanText(value) {
  return String(value || "").replace(/\s+/g, " ").trim();
}

function percentAfterLine(lines, label, maxLookahead, usedValue) {
  const index = lines.findIndex((line) => label.test(line));
  if (index < 0) {
    return null;
  }
  const end = Math.min(lines.length, index + maxLookahead + 1);
  for (let position = index + 1; position < end; position += 1) {
    const value = usedValue ? parseUsedPercent(lines[position]) : parseRemainingPercent(lines[position]);
    if (value !== null) {
      return value;
    }
  }
  return null;
}

function parseUsedPercent(text) {
  const match = text.match(/\b(\d{1,3})\s*%\s*(?:used|usado|utilizado)\b/i) || text.match(/\b(\d{1,3})\s*%\b/);
  if (!match) {
    return null;
  }
  const value = Number(match[1]);
  return value >= 0 && value <= 100 ? value : null;
}

function parseRemainingPercent(text) {
  const match = text.match(/\b(\d{1,3})\s*%\s*(used|remaining|left|available|restante|usado|utilizado)?\b/i);
  if (!match) {
    return null;
  }
  const value = Number(match[1]);
  if (value < 0 || value > 100) {
    return null;
  }
  return /^(?:used|usado|utilizado)$/i.test(match[2] || "") ? 100 - value : value;
}

function firstRemainingPercent(lines) {
  for (const line of lines) {
    const value = parseRemainingPercent(line);
    if (value !== null) {
      return value;
    }
  }
  return null;
}
