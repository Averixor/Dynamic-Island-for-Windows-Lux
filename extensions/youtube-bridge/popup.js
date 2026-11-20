function sendExtension(message) {
  const browserRuntime = globalThis.browser && globalThis.browser.runtime;
  if (browserRuntime && browserRuntime.sendMessage) {
    try {
      const result = browserRuntime.sendMessage(message);
      if (result && typeof result.then === "function") {
        return result.then(
          (response) => response || { connected: false, error: "empty status" },
          (error) => ({ connected: false, error: String(error && error.message ? error.message : error) })
        );
      }
    } catch (error) {
      return Promise.resolve({
        connected: false,
        error: String(error && error.message ? error.message : error),
      });
    }
  }
  const chromeRuntime = globalThis.chrome && globalThis.chrome.runtime;
  return new Promise((resolve) => {
    try {
      chromeRuntime.sendMessage(message, (response) => {
        const runtimeError = chromeRuntime.lastError;
        if (runtimeError) {
          resolve({ connected: false, error: runtimeError.message || String(runtimeError) });
          return;
        }
        resolve(response || { connected: false, error: "empty status" });
      });
    } catch (error) {
      resolve({ connected: false, error: String(error) });
    }
  });
}
const dot = document.getElementById("dot");
const status = document.getElementById("status");
const title = document.getElementById("title");
const meta = document.getElementById("meta");
const portInput = document.getElementById("port");
const hint = document.getElementById("hint");

function likeLabel(value) {
  if (value === "liked") {
    return "Liked";
  }
  if (value === "disliked") {
    return "Disliked";
  }
  return "Not rated";
}

function render(payload) {
  const track = payload && payload.track;
  dot.className = "dot " + (payload && payload.connected ? "on" : "off");
  if (payload && payload.connected) {
    status.textContent = "Connected to 127.0.0.1:" + payload.port;
  } else {
    status.textContent = "Not connected" + (payload && payload.error ? " — " + payload.error : "");
  }
  if (!track) {
    title.textContent = "No track yet";
    meta.textContent = "Open a YouTube or YouTube Music tab.";
  } else {
    title.textContent = track.title || "Untitled";
    const site = track.site === "youtube_music" ? "YouTube Music" : "YouTube";
    const play = track.playing ? "Playing" : "Paused";
    meta.textContent =
      (track.artist || "Unknown artist") + " · " + site + " · " + play + " · " + likeLabel(track.likeState);
  }
  if (payload && payload.port && document.activeElement !== portInput) {
    portInput.value = String(payload.port);
  }
  hint.textContent = payload && payload.connected
    ? "Like and dislike on the expanded island control this browser tab."
    : "Start Dynamic Island Lux and match Modules.MediaLikeBridgePort to this port.";
}

function askStatus() {
  sendExtension({ type: "status" }).then((payload) => {
    render(payload || { connected: false, error: "empty status" });
  });
}

document.getElementById("save").addEventListener("click", () => {
  const port = Number(portInput.value);
  sendExtension({ type: "setPort", port }).then((payload) => {
    render(payload || { connected: false, error: "could not save port" });
  });
});

askStatus();
setInterval(askStatus, 700);
