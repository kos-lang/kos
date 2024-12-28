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

* Add `kos` file type by adding the following line in `~/.config/nvim/lua/config/options.lua`:

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
                    config = function(_, opts)
                        if type(opts.ensure_installed) == "table" then
                            opts.ensure_installed = LazyVim.dedup(opts.ensure_installed)
                        end
                        require("nvim-treesitter.configs").setup(opts)

                        local parser_config = require("nvim-treesitter.parsers").get_parser_configs()
                        parser_config.kos = {
                            install_info = {
                                url = "https://github.com/kos-lang/tree-sitter-kos",
                                files = { "src/parser.c" },
                                branch = "main",
                                generate_requires_npm = false,
                                requires_generate_from_grammar = false,
                            },
                            filetype = "kos",
                        }
                    end
                }
            }
        }

* If needed, run `:TSInstall`.

* Copy queries from [queries on GitHub](https://github.com/kos-lang/tree-sitter-kos/tree/main/queries)
  to `~/.local/share/nvim/lazy/nvim-treesitter/queries/kos` directory.  Create this
  directory if it does not exist.
  (On Windows this is in `~\AppData\Local\nvim-data\lazy\nvim-treesitter\queries\kos`).
  This is only valid if using LazyVim plugin manager, the `nvim-treesitter/queries`
  directory is specific to the `nvim-treesitter` plugin, but its location depends
  on the plugin manager.

Visual Studio Code
------------------

Copy `tools/Kos.tmbundle` directory to:

* `%USERPROFILE%\.vscode\extensions` (Windows)
* `$HOME/.vscode/extensions` (Linux, macOS).

Remove the `.tmbundle` extension from directory name.
