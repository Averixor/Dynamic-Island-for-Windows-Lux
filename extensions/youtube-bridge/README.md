# YouTube bridge for Dynamic Island Lux

Companion extension for the Like and Dislike buttons on the expanded media player. It reads the real Like / Dislike controls on YouTube and YouTube Music, reports that state to Dynamic Island Lux on `127.0.0.1`, and clicks those same controls when the island sends a command.

Nothing is uploaded. The only network target is the loopback bridge inside Dynamic Island Lux.

## What you need

- Dynamic Island Lux running on Windows, with **YouTube / YouTube Music like bridge** enabled.
- The port in the extension popup matching **Like bridge loopback port** (default **18473**).
- A Chromium browser (Chrome, Edge, Brave, Opera, Vivaldi) or Firefox 115+.
- The YouTube tab must stay loaded. A discarded tab does not run the content script.

Installed YouTube and YouTube Music apps (Chrome/Edge PWAs) are normal `https://www.youtube.com` and `https://music.youtube.com` windows. The extension runs there when it is enabled in the **same browser profile** that installed the app.

## Install on Chrome, Edge, Brave, Opera, or Vivaldi

1. Open the extensions page:
   - Chrome: `chrome://extensions`
   - Edge: `edge://extensions`
   - Brave: `brave://extensions`
   - Opera: `opera://extensions`
   - Vivaldi: `vivaldi://extensions`
2. Turn on **Developer mode**.
3. Click **Load unpacked** and choose this `extensions/youtube-bridge` folder (`manifest.json` must be inside it).
4. Pin the extension if you want the popup handy.
5. Open a YouTube or YouTube Music watch page and start playback.
6. Open the extension popup. It should say **Connected to 127.0.0.1:18473** and show the current title, artist, and like state.
7. Expand the island. Like and Dislike sit on the right of the title row. A filled icon means the bridge currently sees that rating. A dim outline means this track has no connected YouTube session — the island will not pretend Windows media keys liked it.

## Install on Firefox

Firefox 115 and newer run this folder's Manifest V3 service worker (`background.service_worker` in `manifest.json`). `browser_specific_settings` gives the extension a stable id. `manifest.firefox.json` is the same document if a workflow expects a separate file. The extension uses `browser.runtime` promises in Firefox and callback messaging in Chromium.

1. Open `about:debugging#/runtime/this-firefox`.
2. Click **Load Temporary Add-on…**.
3. Select `manifest.json` inside this folder.
4. Temporary add-ons are removed when Firefox exits. Load it again next launch, or package it for a permanent install with the id `dynamic-island-lux-bridge@averixor.github.io`.

## Change the port

1. In Dynamic Island Lux settings, set **Like bridge loopback port** (1024–65535) and apply.
2. Open the extension popup, enter the same port, and click **Save**.
3. The popup reconnects on its own. If it stays disconnected, reload the extension from the extensions page.

## Troubleshooting

- **Popup says not connected.** Start Dynamic Island Lux and leave the like bridge enabled. Confirm both sides use the same port. A browser tab opened directly to `http://127.0.0.1:18473` is rejected on purpose; only the extension may call the bridge.
- **Buttons on the island stay dim.** The system media session must be the YouTube/YouTube Music track (or a browser session whose title matches that tab). Spotify and other players do not get a fake like. The YouTube tab also has to be loaded, not discarded.
- **Like state is wrong or clicks do nothing.** Reload the YouTube page. YouTube and YouTube Music are single-page apps; the content script watches navigation, but a hard refresh clears a stuck player DOM.
- **Several YouTube tabs are open.** The island follows the tab that is playing and whose title matches the current system media session.
- **PWA does nothing.** Install the unpacked extension in the same profile that owns the YouTube app window, then restart the app window.
- **Firewall prompt.** Allow `DynamicIslandLux.exe` to listen on localhost only. The bridge never binds `0.0.0.0`.
- **After a Dynamic Island restart.** The extension backs off and reconnects within a few seconds. Leave the YouTube tab open.
