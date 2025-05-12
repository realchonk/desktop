local paqpath = vim.fn.stdpath("data") .. "/site/pack/paqs/start/paq-nvim"
if not (vim.uv or vim.loop).fs_stat(paqpath) then
	local paqrepo = "https://github.com/savq/paq-nvim"
	local out = vim.fn.system({ "git", "clone", "--depth=1", paqrepo, paqpath })
	if vim.v.shell_error ~= 0 then
		vim.api.nvim_echo({
			{ "Failed to clone paq-nvim:\n", "ErrorMsg" },
			{ out, "WarningMsg" },
			{ "\nPress any key to exit..." },
		}, true, {})
		vim.fn.getchar()
		os.exit(1)
	end
	vim.opt.rtp:prepend(paqpath)
end

require "paq" {
	"savq/paq-nvim",
	"neovim/nvim-lspconfig",
	"ethanholz/nvim-lastplace",
	{ "nvim-treesitter/nvim-treesitter", build = ":TSUpdate" },
}

vim.cmd("PaqInstall")

lspconfig = require 'lspconfig'
lspconfig.awk_ls.setup{}
lspconfig.bashls.setup{}
lspconfig.clangd.setup{}
lspconfig.dotls.setup{}
lspconfig.rust_analyzer.setup{
	on_attach = on_attach,
	capabilities = capabilities,
}
lspconfig.svls.setup {
	root_dir = function(fname) 
		return vim.fs.dirname(vim.fs.find('Makefile', { path = fname, upward = true })[1])
	end
}
lspconfig.pylsp.setup{}
--lspconfig.svlangserver.setup{}

require'nvim-lastplace'.setup{
	lastplace_ignore_buftype = {"quickfix", "nofile", "help"},
	lastplace_ignore_filetype = {"gitcommit", "girebase"},
	lastplace_open_folds = true,
}

vim.opt.clipboard = 'unnamedplus'
vim.opt.number = true
vim.opt.relativenumber = true
vim.opt.smartindent = true
vim.opt.ruler = true
vim.cmd("colorscheme vim")

-- Close the completion preview window
vim.cmd("autocmd! CompleteDone * if pumvisible() == 0 | pclose | endif")

require "nvim-treesitter.configs".setup {
	ensure_installed = { "c", "lua", "markdown", "markdown_inline", "vim", "vimdoc", "rust", "make", "nasm" },
	sync_install = true,
	auto_install = true,
	ignore_install = {},

	highlight = {
		enable = true,
		additional_vim_regex_highlighting = false,
	},
}
