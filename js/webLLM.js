window.askLLM = async function (promptText, platform) {
  // 1. DOM Configuration Profiles
  const CONFIG = {
    chatgpt: {
      assistantMsg: 'div[data-message-author-role="assistant"]',
      input: "#prompt-textarea",
      sendBtn: 'button[data-testid*="send-button"]',
      stopBtn: '[aria-label="Stop generating"]',
      streamingFlag: ".result-streaming",
      getText: (el) => el.querySelector(".markdown")?.innerText || el.innerText,
    },
    gemini: {
      assistantMsg: "model-response, .model-response-text",
      input: 'rich-textarea, div[contenteditable="true"][aria-label*="Input"]',
      sendBtn: 'button[aria-label*="Send message"]',
      stopBtn: 'button[aria-label*="Stop generating"]',
      streamingFlag: ".generating-indicator",
      getText: (el) => el.innerText,
    },
    grok: {
      assistantMsg: '.message-bubble, [class*="response"] .markdown, div[data-testid*="message"], article',
      input: 'div[role="textbox"], div[contenteditable="true"], textarea, [data-testid*="input"]',
      sendBtn: 'button[aria-label="Submit"], button[type="submit"], button[data-testid*="send"], button svg path[d*="send"]',
      stopBtn: 'button[aria-label*="Stop"]',
      streamingFlag: null,
      getText: (el) => el.innerText,
      setInput: (el, text) => {
        el.focus();
        const dt = new DataTransfer();
        dt.setData("text/plain", text);
        el.dispatchEvent(
          new ClipboardEvent("paste", { clipboardData: dt, bubbles: true }),
        );
      },
    },
    claude: {
      assistantMsg: ".font-claude-message",
      input: 'div[contenteditable="true"]',
      sendBtn: 'button[aria-label*="Send Message"]',
      stopBtn: 'button[aria-label*="Stop"]',
      streamingFlag: ".typing-indicator",
      getText: (el) => el.innerText,
    },
  };

  const activeCfg = CONFIG[platform.toLowerCase()];
  if (!activeCfg) throw new Error(`Platform ${platform} not configured.`);

  const getMsgs = () => document.querySelectorAll(activeCfg.assistantMsg);
  const initialCount = getMsgs().length;
  const inputEl = document.querySelector(activeCfg.input);
  if (!inputEl) throw new Error(`Input not found for ${platform}`);
  inputEl.focus();

  // Try Native Setter first (Best for React textareas like Grok/ChatGPT)
  if (inputEl.tagName === "TEXTAREA") {
    const nativeSetter = Object.getOwnPropertyDescriptor(
      window.HTMLTextAreaElement.prototype,
      "value",
    ).set;
    if (nativeSetter) nativeSetter.call(inputEl, promptText);
  } else {
    // Fallback for ContentEditable (Gemini/Claude)
    document.execCommand("insertText", false, promptText);
  }

  // Fire a barrage of events to wake up the framework's state manager
  inputEl.dispatchEvent(new Event("input", { bubbles: true }));
  inputEl.dispatchEvent(new Event("change", { bubbles: true }));
  inputEl.dispatchEvent(new Event("compositionend", { bubbles: true }));

  await new Promise((r) => setTimeout(r, 600));

  const sendBtn = document.querySelector(activeCfg.sendBtn);

  // Check for standard disabled AND modern aria-disabled
  const isEffectivelyDisabled =
    !sendBtn ||
    sendBtn.disabled ||
    sendBtn.getAttribute("aria-disabled") === "true" ||
    sendBtn.hasAttribute("disabled");

  if (sendBtn && !isEffectivelyDisabled) {
    // Dispatch full MouseEvents to bypass frameworks that ignore .click()
    sendBtn.dispatchEvent(
      new MouseEvent("mousedown", {
        bubbles: true,
        cancelable: true,
        view: window,
      }),
    );
    sendBtn.dispatchEvent(
      new MouseEvent("mouseup", {
        bubbles: true,
        cancelable: true,
        view: window,
      }),
    );
    sendBtn.click();
  } else {
    console.warn(
      `[${platform}] Send button blocked or missing. Forcing Enter key fallback.`,
    );

    const enterEventPayload = {
      key: "Enter",
      code: "Enter",
      keyCode: 13,
      which: 13,
      bubbles: true,
      cancelable: true,
    };
    inputEl.dispatchEvent(new KeyboardEvent("keydown", enterEventPayload));
    inputEl.dispatchEvent(new KeyboardEvent("keypress", enterEventPayload));
    inputEl.dispatchEvent(new KeyboardEvent("keyup", enterEventPayload));
  }

  console.log(`[${platform}] Waiting for generation to start...`);

  // Observe for new messages
  return new Promise((resolve) => {
    let hasStarted = false;
    let stableCount = 0;
    let lastText = "";

    const checkInterval = setInterval(() => {
      const msgs = getMsgs();
      const stopBtn = document.querySelector(activeCfg.stopBtn);
      const isStreaming = activeCfg.streamingFlag
        ? document.querySelector(activeCfg.streamingFlag)
        : null;

      if (msgs.length > initialCount || stopBtn || isStreaming) {
        hasStarted = true;
      }

      if (hasStarted && !stopBtn && !isStreaming) {
        const currentMsg = msgs[msgs.length - 1];
        if (!currentMsg) return;

        const currentText = activeCfg.getText(currentMsg) || "";

        if (currentText.length > 0 && currentText === lastText) {
          stableCount++;
        } else {
          lastText = currentText;
          stableCount = 0;
        }

        if (stableCount >= 3) {
          clearInterval(checkInterval);
          resolve(currentText.trim());
        }
      }
    }, 500);
  });
};
