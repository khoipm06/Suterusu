# Suterusu

Communicates with AI using your system clipboard (Windows only)

## Features

- 100% vibe-coded with Claude Sonnet 4.5, with bug-fixes made by Gemini 3 Pro.
- Compatible with OpenAI-like API endpoints (llama.cpp, etc.).
- Visual overlay feedback (subtle pulsing indicator in bottom-right corner)
- Persistent CURL connections for low latency
- Model fallback support (automatically tries backup models if primary fails)
- JavaScript injection via Chrome DevTools Protocol (CDP)
- Auto-load startup scripts for custom browser automation
- It works on my machine.

## Usage

Read `config.json` for more information.

Keybinds:
- **F6**: Clear chat history
- **F7**: Send clipboard content to AI
- **F8**: Copy AI response to clipboard
- **F9**: Toggle text selection highlighting (handled by JavaScript)
- **F12**: Close the application

## JavaScript Scripts

Scripts in `js/startup/` are automatically injected into the browser on launch.

## Installation

TODO

## Building

TODO

## License

[MIT](./LICENSE)
