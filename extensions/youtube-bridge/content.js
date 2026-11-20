(function () {
  const ext = globalThis.browser || globalThis.chrome;
  if (!ext || !ext.runtime || !ext.runtime.sendMessage) {
    return;
  }

  const site = location.hostname === "music.youtube.com" ? "youtube_music" : "youtube";
  const pendingAck = [];
  const seen = new Set();
  let applying = false;
  let scanTimer = 0;
  let watched = null;
  let lastSignature = "";
  let lastSentAt = 0;

  function textOf(node) {
    return ((node && node.textContent) || "").replace(/\s+/g, " ").trim();
  }

  function firstText(selectors, root) {
    const scope = root || document;
    for (let i = 0; i < selectors.length; i++) {
      const text = textOf(scope.querySelector(selectors[i]));
      if (text) {
        return text;
      }
    }
    return "";
  }

  function currentVideoId() {
    try {
      const url = new URL(location.href);
      const watchId = url.searchParams.get("v");
      if (watchId) {
        return watchId;
      }
      const shorts = location.pathname.match(/\/shorts\/([^/?#]+)/);
      if (shorts) {
        return shorts[1];
      }
    } catch (error) {
      return "";
    }
    return "";
  }

  function titleFromDocument() {
    return (document.title || "")
      .replace(/\s+[-–—]\s+YouTube Music\s*$/i, "")
      .replace(/\s+[-–—]\s+YouTube\s*$/i, "")
      .trim();
  }

  function primaryArtist(raw) {
    const parts = String(raw || "").split(/\s+[•·|]\s+/);
    return (parts[0] || "").trim();
  }

  function queryDeep(root, selector, depth) {
    if (!root || depth > 6 || !root.querySelector) {
      return null;
    }
    const direct = root.querySelector(selector);
    if (direct) {
      return direct;
    }
    const nodes = root.querySelectorAll("*");
    for (let i = 0; i < nodes.length; i++) {
      if (nodes[i].shadowRoot) {
        const found = queryDeep(nodes[i].shadowRoot, selector, depth + 1);
        if (found) {
          return found;
        }
      }
    }
    return null;
  }

  function collectButtons(root, out, depth) {
    if (!root || depth > 6 || !root.querySelectorAll) {
      return;
    }
    const buttons = root.querySelectorAll("button");
    for (let i = 0; i < buttons.length; i++) {
      out.push(buttons[i]);
    }
    const nodes = root.querySelectorAll("*");
    for (let i = 0; i < nodes.length; i++) {
      if (nodes[i].shadowRoot) {
        collectButtons(nodes[i].shadowRoot, out, depth + 1);
      }
    }
  }

  function playerScope() {
    return (
      document.querySelector("ytd-watch-metadata") ||
      document.querySelector("#above-the-fold") ||
      document.querySelector("ytd-reel-player-overlay-renderer") ||
      document.querySelector("ytd-shorts") ||
      document.querySelector("ytmusic-like-button-renderer") ||
      document.querySelector("ytmusic-player-bar") ||
      document
    );
  }

  function buttonFor(tag) {
    const scope = playerScope();
    const host = (scope && scope.querySelector && scope.querySelector(tag)) || document.querySelector(tag);
    if (!host) {
      return null;
    }
    return queryDeep(host, "button[aria-pressed]", 0) || queryDeep(host, "button", 0);
  }

  function musicButtons() {
    const host = document.querySelector("ytmusic-like-button-renderer");
    if (!host) {
      return { like: null, dislike: null, status: "" };
    }
    const all = [];
    collectButtons(host, all, 0);
    const labeled = all.filter(function (button) {
      return button.getAttribute("aria-label") || button.getAttribute("aria-pressed") || button.getAttribute("title");
    });
    const list = labeled.length >= 2 ? labeled : all;
    const unique = [];
    for (let i = 0; i < list.length; i++) {
      if (unique.indexOf(list[i]) === -1) {
        unique.push(list[i]);
      }
    }
    return {
      like: unique[0] || null,
      dislike: unique[1] || null,
      status: (host.getAttribute("like-status") || "").toUpperCase(),
    };
  }

  function readRate() {
    if (site === "youtube_music") {
      const music = musicButtons();
      let likeState = "none";
      if (music.status === "LIKE") {
        likeState = "liked";
      } else if (music.status === "DISLIKE") {
        likeState = "disliked";
      }
      return {
        likeState: likeState,
        canRate: !!(music.like && music.dislike),
        like: music.like,
        dislike: music.dislike,
      };
    }
    const like = buttonFor("like-button-view-model");
    const dislike = buttonFor("dislike-button-view-model");
    const likePressed = !!(like && like.getAttribute("aria-pressed") === "true");
    const dislikePressed = !!(dislike && dislike.getAttribute("aria-pressed") === "true");
    let likeState = "none";
    if (likePressed) {
      likeState = "liked";
    } else if (dislikePressed) {
      likeState = "disliked";
    }
    return {
      likeState: likeState,
      canRate: !!(like && dislike),
      like: like,
      dislike: dislike,
    };
  }

  function readMetadata() {
    const video = document.querySelector("video");
    const playing = !!(video && !video.paused && !video.ended);
    if (site === "youtube_music") {
      const title =
        firstText([
          "ytmusic-player-bar .title",
          ".ytmusic-player-bar .title",
          "ytmusic-player-bar yt-formatted-string.title",
        ]) || titleFromDocument();
      const artist = primaryArtist(
        firstText([
          "ytmusic-player-bar .byline",
          "ytmusic-player-bar yt-formatted-string.byline",
          ".ytmusic-player-bar .subtitle",
          ".ytmusic-player-bar .byline",
        ])
      );
      return { title: title, artist: artist, playing: playing };
    }
    const title =
      firstText([
        "ytd-watch-metadata h1 yt-formatted-string",
        "h1.ytd-watch-metadata yt-formatted-string",
        "#title h1 yt-formatted-string",
        "h1.ytd-video-primary-info-renderer",
        "yt-shorts-video-title span",
        "ytd-reel-player-header-renderer h2 yt-formatted-string",
      ]) || titleFromDocument();
    const artist = firstText([
      "ytd-watch-metadata ytd-channel-name a",
      "#owner ytd-channel-name a",
      "ytd-video-owner-renderer ytd-channel-name a",
      "ytd-reel-player-header-renderer ytd-channel-name a",
    ]);
    return { title: title, artist: artist, playing: playing };
  }

  function collect() {
    const meta = readMetadata();
    const rate = readRate();
    return {
      site: site,
      videoId: currentVideoId(),
      title: meta.title,
      artist: meta.artist,
      playing: meta.playing,
      likeState: rate.likeState,
      canRate: rate.canRate,
    };
  }

  function send(message) {
    const browserRuntime = globalThis.browser && globalThis.browser.runtime;
    if (browserRuntime && browserRuntime.sendMessage) {
      try {
        const result = browserRuntime.sendMessage(message);
        if (result && typeof result.then === "function") {
          return result.then(
            function (response) {
              return response || { ok: false, error: "empty" };
            },
            function (error) {
              return { ok: false, error: String(error && error.message ? error.message : error) };
            }
          );
        }
      } catch (error) {
        return Promise.resolve({ ok: false, error: String(error && error.message ? error.message : error) });
      }
    }
    const chromeRuntime = globalThis.chrome && globalThis.chrome.runtime;
    return new Promise(function (resolve) {
      try {
        chromeRuntime.sendMessage(message, function (response) {
          const runtimeError = chromeRuntime.lastError;
          if (runtimeError) {
            resolve({ ok: false, error: runtimeError.message || String(runtimeError) });
            return;
          }
          resolve(response || { ok: false, error: "empty" });
        });
      } catch (error) {
        resolve({ ok: false, error: String(error) });
      }
    });
  }

  function delay(ms) {
    return new Promise(function (resolve) {
      setTimeout(resolve, ms);
    });
  }

  function press(target) {
    if (!target) {
      return;
    }
    const view = target.ownerDocument && target.ownerDocument.defaultView ? target.ownerDocument.defaultView : window;
    const options = { bubbles: true, cancelable: true, view: view };
    if (typeof view.PointerEvent === "function") {
      target.dispatchEvent(new view.PointerEvent("pointerdown", options));
    }
    target.dispatchEvent(new view.MouseEvent("mousedown", options));
    if (typeof view.PointerEvent === "function") {
      target.dispatchEvent(new view.PointerEvent("pointerup", options));
    }
    target.dispatchEvent(new view.MouseEvent("mouseup", options));
    target.click();
  }

  function clickKey(id) {
    return "dilux-rate-" + id;
  }

  function clickStamp(id) {
    try {
      const value = Number(sessionStorage.getItem(clickKey(id)));
      return Number.isFinite(value) ? value : 0;
    } catch (error) {
      return 0;
    }
  }

  function rememberClick(id) {
    try {
      sessionStorage.setItem(clickKey(id), String(Date.now()));
    } catch (error) {
      seen.add(id);
    }
  }

  function forgetClick(id) {
    try {
      sessionStorage.removeItem(clickKey(id));
    } catch (error) {
      seen.delete(id);
    }
  }

  function queueAck(id) {
    if (pendingAck.indexOf(id) === -1) {
      pendingAck.push(id);
    }
  }

  async function runCommand(action) {
    const before = readRate();
    const target = action === "dislike" ? before.dislike : before.like;
    if (!target) {
      return false;
    }
    press(target);
    const started = Date.now();
    while (Date.now() - started < 900) {
      await delay(70);
      if (readRate().likeState !== before.likeState) {
        return true;
      }
    }
    return false;
  }

  async function handleCommands(commands) {
    if (!Array.isArray(commands) || commands.length === 0) {
      return;
    }
    applying = true;
    try {
      for (let i = 0; i < commands.length; i++) {
        const command = commands[i];
        if (!command || command.id == null) {
          continue;
        }
        const videoId = currentVideoId();
        if (command.videoId && videoId && command.videoId !== videoId) {
          queueAck(command.id);
          continue;
        }
        if (clickStamp(command.id) > 0) {
          queueAck(command.id);
          continue;
        }
        const before = readRate();
        const target = command.action === "dislike" ? before.dislike : before.like;
        if (!target) {
          continue;
        }
        rememberClick(command.id);
        const applied = await runCommand(command.action === "dislike" ? "dislike" : "like");
        if (applied) {
          queueAck(command.id);
        } else {
          forgetClick(command.id);
        }
      }
    } finally {
      applying = false;
    }
  }

  function trustedBridge(response) {
    return !!(response && response.ok === true && response.service === "dynamic-island-lux");
  }

  function dropAcked(sent) {
    for (let i = pendingAck.length - 1; i >= 0; i--) {
      if (sent.indexOf(pendingAck[i]) !== -1) {
        pendingAck.splice(i, 1);
      }
    }
  }

  async function push(force) {
    if (applying) {
      return;
    }
    const state = collect();
    const signature = JSON.stringify(state);
    const now = Date.now();
    if (!force && signature === lastSignature && now - lastSentAt < 800) {
      return;
    }
    lastSignature = signature;
    lastSentAt = now;
    const ack = pendingAck.slice();
    const response = await send({
      type: "state",
      state: {
        site: state.site,
        videoId: state.videoId,
        title: state.title,
        artist: state.artist,
        playing: state.playing,
        likeState: state.likeState,
        canRate: state.canRate,
        timestamp: Date.now(),
        ack: ack,
      },
    });
    if (!trustedBridge(response)) {
      return;
    }
    dropAcked(ack);
    if (Array.isArray(response.commands) && response.commands.length) {
      await handleCommands(response.commands);
      const fresh = collect();
      const followAck = pendingAck.slice();
      const follow = await send({
        type: "state",
        state: {
          site: fresh.site,
          videoId: fresh.videoId,
          title: fresh.title,
          artist: fresh.artist,
          playing: fresh.playing,
          likeState: fresh.likeState,
          canRate: fresh.canRate,
          timestamp: Date.now(),
          ack: followAck,
        },
      });
      if (trustedBridge(follow)) {
        dropAcked(followAck);
      }
      lastSignature = JSON.stringify(fresh);
      lastSentAt = Date.now();
    }
  }

  let pushRunning = false;
  let pushQueued = false;
  let queuedForce = false;

  function requestPush(force) {
    if (pushRunning) {
      pushQueued = true;
      queuedForce = queuedForce || !!force;
      return;
    }
    pushRunning = true;
    const useForce = !!force;
    Promise.resolve()
      .then(async function () {
        let nextForce = useForce;
        do {
          pushQueued = false;
          const runForce = nextForce;
          nextForce = false;
          queuedForce = false;
          await push(runForce);
          if (pushQueued) {
            nextForce = queuedForce;
          }
        } while (pushQueued);
      })
      .then(
        function () {
          pushRunning = false;
        },
        function () {
          pushRunning = false;
        }
      );
  }

  function schedule() {
    if (scanTimer) {
      return;
    }
    scanTimer = setTimeout(function () {
      scanTimer = 0;
      watchRoot();
      requestPush(false);
    }, 250);
  }

  function watchRoot() {
    const root =
      document.querySelector("ytd-watch-metadata") ||
      document.querySelector("ytd-shorts") ||
      document.querySelector("ytmusic-player-bar") ||
      document.querySelector("ytmusic-app") ||
      document.body;
    if (!root || root === watched) {
      return;
    }
    observer.disconnect();
    observer.observe(root, {
      subtree: true,
      childList: true,
      attributes: true,
      attributeFilter: ["aria-pressed", "like-status", "title"],
    });
    watched = root;
  }

  const observer = new MutationObserver(function () {
    schedule();
  });

  document.addEventListener("yt-navigate-finish", function () {
    watchRoot();
    schedule();
  });
  window.addEventListener("popstate", function () {
    watchRoot();
    schedule();
  });
  document.addEventListener("play", schedule, true);
  document.addEventListener("pause", schedule, true);

  watchRoot();
  requestPush(true);
  setInterval(function () {
    if (document.hidden && Date.now() - lastSentAt < 2400) {
      return;
    }
    requestPush(true);
  }, 1000);
})();
