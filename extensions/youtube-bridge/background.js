const DEFAULT_PORT = 18473;
const ext = globalThis.browser || globalThis.chrome;

const tabs = new Map();
let connected = false;
let lastError = "";
let backoffMs = 0;
let nextTryAt = 0;
let port = DEFAULT_PORT;

function clampPort(value) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1024 || parsed > 65535) {
    return DEFAULT_PORT;
  }
  return parsed;
}

function readPort() {
  return ext.storage.local.get({ bridgePort: DEFAULT_PORT }).then((stored) => {
    port = clampPort(stored.bridgePort);
    return port;
  });
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
  const now = Date.now();
  for (const [id, row] of tabs) {
    if (now - row.at > 15000) {
      tabs.delete(id);
    }
  }
}

function currentTrack() {
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

async function postState(state) {
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
    timestamp: Date.now(),
    ack: Array.isArray(state.ack) ? state.ack : [],
  };
  try {
    const response = await fetch("http://127.0.0.1:" + currentPort + "/v1/state", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Dynamic-Island-Lux": "1",
      },
      body: JSON.stringify(payload),
      cache: "no-store",
    });
    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }
    const data = await response.json();
    connected = true;
    lastError = "";
    backoffMs = 0;
    nextTryAt = 0;
    return data && typeof data === "object" ? data : { ok: true, commands: [] };
  } catch (error) {
    connected = false;
    lastError = error && error.message ? String(error.message) : String(error);
    backoffMs = backoffMs ? Math.min(5000, backoffMs * 2) : 400;
    nextTryAt = Date.now() + backoffMs;
    return { ok: false, error: lastError, commands: [] };
  }
}

ext.runtime.onMessage.addListener((message, sender, sendResponse) => {
  const finish = (payload) => {
    try {
      sendResponse(payload);
    } catch (error) {
      // The popup can close before the reply lands.
    }
  };
  if (!message || message.type === "status") {
    readPort().then(() => finish(statusPayload()));
    return true;
  }
  if (message.type === "setPort") {
    const next = clampPort(message.port);
    ext.storage.local.set({ bridgePort: next }).then(() => {
      port = next;
      backoffMs = 0;
      nextTryAt = 0;
      finish(statusPayload());
    });
    return true;
  }
  if (message.type === "state" && message.state) {
    const state = Object.assign({}, message.state);
    if (sender && sender.tab && typeof sender.tab.id === "number") {
      state.tabId = "tab-" + sender.tab.id;
    }
    postState(state).then(finish);
    return true;
  }
  finish({ ok: false, error: "unknown message" });
  return true;
});

readPort();
