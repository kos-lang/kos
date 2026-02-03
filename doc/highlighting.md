Syntax Highlighting
===================

This document explains how to install syntax highlighting for Kos in many popular editors.

Sublime Text
------------

Copy `tools/Kos.tmbundle` directory to:

* `%USERPROFILE%\AppData\Roaming\Sublime Text 3\Packages` (Windows)
* `$HOME/.config/sublime-text-3/Packages` (Linux)
* `~/Library/Application\ Support/Sublime\ Text/Packages` (macOS).

Remove the `.tmbundle` extension from directory name.

TextMate
--------

Copy the whole `tools/Kos.tmbundle` directory to `~/Library/Application\ Support/TextMate/Bundles`
directory.

Vim
---

Copy the contents of `tools/vim` directory to:

* `$HOME\.vim` (Linux, macOS).
* `%USERPROFILE%/_vim` (Windows).

NeoVim
------

* NeoVim configuration is located in `~/.config/nvim` on Linux and MacOS.
  On Windows the same directory is located in `~\AppData\Local\nvim`.

* The latest NeoVim should detect `kos` file type automatically.
  If it does not, add `kos` file type by adding the following line in `~/.config/nvim/lua/config/options.lua`:

        vim.filetype.add({ extension = { kos = "kos" } })

* Configure `nvim-treesitter` with `kos` support in `~/.config/nvim/lua/plugins/tree-sitter.lua`
  (or similar file of your choice in that directory):

        return {
            {
                "nvim-treesitter/nvim-treesitter",
                opts = {
                    ensure_installed = {
                        "kos"
                        -- enable more languages as needed
                    },
                }
            }
        }

* If needed, run `:TSInstall`.

Visual Studio Code
------------------

Copy `tools/Kos.tmbundle` directory to:

* `%USERPROFILE%\.vscode\extensions` (Windows)
* `$HOME/.vscode/extensions` (Linux, macOS).

Remove the `.tmbundle` extension from directory name.
