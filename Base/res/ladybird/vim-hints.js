// Labels each visible element that can be clicked with a hint, and follows the hint whose keys are typed: It clicks or
// focuses the element, opens its link in a new tab, or copies the URL of its link. The vim keybindings run this in the
// page, and let the keys of the hint through to it, so that as real key presses they may open a tab or copy.
function showHints(mode, hintLength) {
    const keys = "asdfghjklqwertyuiopzxcvbnm";
    const selector =
        mode === "yank"
            ? "a[href]"
            : "a[href], button, input:not([type=hidden]), select, textarea, summary, label, [onclick], [role=button], " +
              "[role=link], [tabindex]:not([tabindex='-1']), [contenteditable]:not([contenteditable=false])";

    const overlay = document.createElement("div");
    overlay.style.cssText = "position: fixed; inset: 0; z-index: 2147483647; pointer-events: none";

    const addLabel = (text, position) => {
        const label = overlay.appendChild(document.createElement("span"));
        label.textContent = text;
        label.style.cssText = `position: absolute; ${position}; padding: 0 2px; font: bold 12px monospace; color: black; background: #fd4; border: 1px solid #a80; border-radius: 2px`;
        return label;
    };

    const hintForIndex = index => {
        let hint = "";
        for (let i = 0; i < hintLength; ++i) {
            hint += keys[index % keys.length];
            index = Math.floor(index / keys.length);
        }
        return hint;
    };

    const targets = [];
    for (const element of document.querySelectorAll(selector)) {
        const rect = element.getClientRects()[0];
        const isInView = rect && rect.bottom > 0 && rect.right > 0 && rect.top < innerHeight && rect.left < innerWidth;
        if (!isInView || targets.length === keys.length ** hintLength) {
            continue;
        }
        const hint = hintForIndex(targets.length);
        targets.push({ element, hint, label: addLabel(hint, `left: ${rect.left}px; top: ${rect.top}px`) });
    }
    document.documentElement.append(overlay);

    const copyLink = element => {
        const copy = event => {
            event.clipboardData.setData("text/plain", element.href);
            event.preventDefault();
        };
        addEventListener("copy", copy, true);
        const copied = document.execCommand("copy");
        removeEventListener("copy", copy, true);

        overlay.replaceChildren();
        addLabel(copied ? `Copied ${element.href}` : "Could not copy the link", "right: 8px; bottom: 8px");
        document.documentElement.append(overlay);
        setTimeout(() => overlay.remove(), 1500);
    };

    const follow = element => {
        if (mode === "yank") {
            copyLink(element);
        } else if (mode === "tab" && element instanceof HTMLAnchorElement) {
            window.open(element.href, "_blank", "noopener");
        } else if (element.matches("input, select, textarea, [contenteditable]")) {
            element.focus();
        } else {
            element.click();
        }
    };

    let typed = "";
    const onKeyDown = event => {
        if (["Shift", "Control", "Alt", "Meta"].includes(event.key)) {
            return;
        }
        event.preventDefault();
        event.stopImmediatePropagation();

        // A key that is no character, such as Escape, matches no hint.
        typed += event.key.length === 1 ? event.key.toLowerCase() : "\0";
        const matches = targets.filter(target => target.hint.startsWith(typed));
        for (const target of targets) {
            target.label.style.display = matches.includes(target) ? "" : "none";
        }
        if (matches.length > 0 && typed.length < hintLength) {
            return;
        }

        overlay.remove();
        removeEventListener("keydown", onKeyDown, true);
        if (matches.length > 0) {
            follow(matches[0].element);
        }
    };
    addEventListener("keydown", onKeyDown, true);
}
