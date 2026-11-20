const DEFAULT_PORT = 18473;
const BRIDGE_SERVICE = "dynamic-island-lux";
const REQUEST_TIMEOUT_MS = 2500;
const runtime = (globalThis.browser && globalThis.browser.runtime) || globalThis.chrome.runtime;
const storage = (globalThis.browser && globalThis.browser.storage) || globalThis.chrome.storage;

const tabs = new Map();
let connected = false;
let lastError = "";
let backoffMs = 0;
let nextTryAt = 0;
let port = DEFAULT_PORT;
let instancePromise = null;
let postChain = Promise.resolve();

function clampPort(value) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1024 || parsed > 65535) {
    return DEFAULT_PORT;
  }
  return parsed;
}

function readPort() {
  return storage.local.get({ bridgePort: DEFAULT_PORT }).then((stored) => {
    port = clampPort(stored.bridgePort);
    return port;
  });
}

function browserInstanceId() {
  if (!instancePromise) {
    instancePromise = storage.local.get({ bridgeInstanceId: "" }).then((stored) => {
      if (stored.bridgeInstanceId) {
        return stored.bridgeInstanceId;
      }
      const created = "b" + Math.random().toString(36).slice(2) + Date.now().toString(36);
      return storage.local.set({ bridgeInstanceId: created }).then(() => created);
    });
  }
  return instancePromise;
}

function expireTabs() {
  const now = Date.now();
  for (const [id, row] of tabs) {
    if (now - row.at > 15000) {
      tabs.delete(id);
    }
  }
}

function remember(state) {
  if (!state || !state.tabId) {
    return;
  }
  tabs.set(state.tabId, {
    tabId: state.tabId,
    site: state.site,
    videoId: state.videoId || "",
    title: state.title || "",
    artist: state.artist || "",
    playing: !!state.playing,
    likeState: state.likeState || "none",
    canRate: !!state.canRate,
    at: Date.now(),
  });
  expireTabs();
}

function currentTrack() {
  expireTabs();
  let best = null;
  for (const row of tabs.values()) {
    if (!best) {
      best = row;
      continue;
    }
    if (row.playing && !best.playing) {
      best = row;
      continue;
    }
    if (row.playing === best.playing && row.at > best.at) {
      best = row;
    }
  }
  return best;
}

function statusPayload() {
  return {
    ok: true,
    connected,
    error: lastError,
    port,
    backoffMs,
    track: currentTrack(),
  };
}

function bridgePayloadOk(data) {
  return !!(data && data.ok === true && data.service === BRIDGE_SERVICE);
}

async function probePort(nextPort) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const response = await fetch("http://127.0.0.1:" + nextPort + "/v1/health", {
      method: "GET",
      headers: { "X-Dynamic-Island-Lux": "1" },
      cache: "no-store",
      signal: controller.signal,
    });
    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }
    const data = await response.json();
    if (!bridgePayloadOk(data)) {
      throw new Error("unexpected bridge");
    }
    connected = true;
    lastError = "";
    backoffMs = 0;
    nextTryAt = 0;
  } catch (error) {
    connected = false;
    lastError = error && error.message ? String(error.message) : String(error);
  } finally {
    clearTimeout(timer);
  }
}

async function postStateBody(state) {
  remember(state);
  const now = Date.now();
  if (now < nextTryAt) {
    return { ok: false, error: lastError || "waiting to reconnect", commands: [] };
  }
  const currentPort = await readPort();
  const payload = {
    tabId: state.tabId,
    site: state.site,
    videoId: state.videoId || "",
    title: state.title || "",
    artist: state.artist || "",
    playing: !!state.playing,
    likeState: state.likeState || "none",
    canRate: !!state.canRate,
    timestamp: typeof state.timestamp === "number" ? state.timestamp : Date.now(),
    ack: Array.isArray(state.ack) ? state.ack : [],
  };
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const response = await fetch("http://127.0.0.1:" + currentPort + "/v1/state", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Dynamic-Island-Lux": "1",
      },
      body: JSON.stringify(payload),
      cache: "no-store",
      signal: controller.signal,
    });
    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }
    const data = await response.json();
    if (!bridgePayloadOk(data)) {
      throw new Error("unexpected bridge");
    }
    connected = true;
    lastError = "";
    backoffMs = 0;
    nextTryAt = 0;
    if (!Array.isArray(data.commands)) {
      data.commands = [];
    }
    return data;
  } catch (error) {
    connected = false;
    lastError = error && error.message ? String(error.message) : String(error);
    backoffMs = backoffMs ? Math.min(5000, backoffMs * 2) : 400;
    nextTryAt = Date.now() + backoffMs;
    return { ok: false, error: lastError, commands: [] };
  } finally {
    clearTimeout(timer);
  }
}

function postState(state) {
  const run = postChain.then(() => postStateBody(state));
  postChain = run.then(
    () => undefined,
    () => undefined
  );
  return run;
}

function handleMessage(message, sender) {
  if (!message || message.type === "status") {
    return readPort().then(() => statusPayload());
  }
  if (message.type === "setPort") {
    const next = clampPort(message.port);
    connected = false;
    lastError = "checking port";
    backoffMs = 0;
    nextTryAt = 0;
    return storage.local.set({ bridgePort: next }).then(() => {
      port = next;
      return probePort(next);
    }).then(() => statusPayload());
  }
  if (message.type === "state" && message.state) {
    const state = Object.assign({}, message.state);
    return browserInstanceId().then((instanceId) => {
      if (sender && sender.tab && typeof sender.tab.id === "number") {
        state.tabId = instanceId + ":tab-" + sender.tab.id;
      }
      if (typeof state.timestamp !== "number") {
        state.timestamp = Date.now();
      }
      return postState(state);
    });
  }
  return Promise.resolve({ ok: false, error: "unknown message" });
}

runtime.onMessage.addListener((message, sender, sendResponse) => {
  const task = handleMessage(message, sender);
  if (globalThis.browser && globalThis.browser.runtime === runtime) {
    return task;
  }
  task.then(
    (payload) => {
      try {
        sendResponse(payload);
      } catch (error) {
        // The popup can close before the reply lands.
      }
    },
    (error) => {
      try {
        sendResponse({ ok: false, error: String(error && error.message ? error.message : error) });
      } catch (sendError) {
        // The popup can close before the reply lands.
      }
    }
  );
  return true;
});

readPort();
