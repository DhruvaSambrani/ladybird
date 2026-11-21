/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

class QKeyEvent;

namespace Ladybird {

class BrowserWindow;
class Tab;

// Vim-style keys, which take precedence over the page's own, except in text fields. The "ui.vim" settings turn them on
// and off, which Shift+Escape also does, and rebind them.
namespace VimKeybindings {

bool is_enabled();
// Returns whether the key was taken, so that it doesn't reach the page.
bool handle_key_press(BrowserWindow&, Tab&, QKeyEvent const&);

}

}
