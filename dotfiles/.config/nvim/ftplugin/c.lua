vim.opt.shiftwidth = 8
vim.opt.tabstop = 8
vim.opt.expandtab = false

local bufnr = vim.api.nvim_get_current_buf()

vim.keymap.set("n", "<leader>h", function()
	vim.cmd.ClangdSwitchSourceHeader()
end, { silent = true, buffer = bufnr })
