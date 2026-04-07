window.askGPT = async function(promptText) {
  const getMsgs = () =>
    document.querySelectorAll('div[data-message-author-role="assistant"]');
  const inputEl = document.querySelector("#prompt-textarea");
  if (!inputEl) throw new Error("Input not found");

  const initialCount = getMsgs().length;

  // Should be deprecated but still works
  // document.execCommand('insertText', false, promptText);
  // inputEl.dispatchEvent(new Event('input', { bubbles: true }));
  inputEl.focus();
  if (inputEl.tagName === "TEXTAREA") {
    const nativeSetter = Object.getOwnPropertyDescriptor(
      window.HTMLTextAreaElement.prototype,
      "value",
    ).set;
    nativeSetter.call(inputEl, promptText);
  } else {
    // Safer than innerHTML: uses textContent to avoid HTML injection issues
    const p = document.createElement("p");
    p.textContent = promptText;
    inputEl.innerHTML = "";
    inputEl.appendChild(p);
  }
  inputEl.dispatchEvent(new Event("input", { bubbles: true }));

  // 2. SEND
  await new Promise((r) => setTimeout(r, 600));
  const sendBtn = document.querySelector('button[data-testid*="send-button"]');
  if (sendBtn) sendBtn.click();

  // 3. STABLE OBSERVER
  return new Promise((resolve) => {
    let hasStarted = false;
    let stableCount = 0;
    let lastText = "";

    const checkInterval = setInterval(() => {
      const msgs = getMsgs();
      const stopBtn = document.querySelector('[aria-label="Stop generating"]');
      const isStreaming = document.querySelector(".result-streaming");

      // PHASE 1: Wait for evidence of life
      if (msgs.length > initialCount || stopBtn || isStreaming) {
        hasStarted = true;
      }

      // PHASE 2: Wait for stability
      if (hasStarted && !stopBtn && !isStreaming) {
        const currentMsg = msgs[msgs.length - 1];
        const currentText =
          currentMsg?.querySelector(".markdown")?.innerText ||
          currentMsg?.innerText ||
          "";

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