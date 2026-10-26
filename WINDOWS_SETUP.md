# shadPS4 Windows setup — PlayTorrio PS4 v0.8

On shadPS4 v0.20.0 for Windows, the virtual PS4 path `/data` is mapped to
`%APPDATA%\shadPS4\data` by default.

1. Press **Win+R**, paste `%APPDATA%\shadPS4\data`, and press Enter.
2. Create the subfolder `playtorrio` (if missing).
3. In Notepad, save this file as `settings.json` using **Save as type: All Files**, encoding UTF-8.
4. Its full Windows path must be `%APPDATA%\shadPS4\data\playtorrio\settings.json`.
5. Paste your HTTPS Stremio manifest URL in `addons`, your TMDB v3 key in `tmdbApiKey`, and your TorBox API key only if needed. Set `githubSyncEnabled` to `false`.
6. Start the app, or press **Square** on its Settings page to reload after editing the file. No USB drive needed.

The actual JSON example is at `config/settings.example.json`. Keep this local file private.

GitHub sync is disabled by default in v0.8. Disabling it prevents GitHub reads and writes even if an old GitHub token is still in local settings. You do not need to configure a GitHub token.

Press **Triangle** to test TMDB and Stremio connectivity. See `%APPDATA%\shadPS4\data\playtorrio\logs\diagnostics.log` for status codes. Your credentials are not written to that diagnostic log.

TMDB API connectivity is tested in v0.8, but real TMDB poster browsing and reliable video/audio playback are still incomplete.
