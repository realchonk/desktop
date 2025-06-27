vim.opt.shiftwidth = 4
vim.opt.tabstop = 4
vim.opt.expandtab = false

local bufnr = vim.api.nvim_get_current_buf()

vim.keymap.set("n", "<leader>a", function()
	vim.cmd.RustLsp('codeAction')
end, { silent = true, buffer = bufnr })

vim.keymap.set("n", "<leader>c", function()
	vim.cmd.RustLsp('openCargo')
end, { silent = true, buffer = bufnr })

vim.keymap.set("n", "<leader>d", function()
	vim.cmd.RustLsp('openDocs')
end, { silent = true, buffer = bufnr })

vim.keymap.set("n", "<leader>e", function()
	vim.cmd.RustLsp('expandMacro')
end, { silent = true, buffer = bufnr })

vim.keymap.set("n", "<leader>E", function()
	vim.cmd.RustLsp('renderDiagnostic')
end, { silent = true, buffer = bufnr })

vim.keymap.set("n", "<leader>r", function()
	vim.cmd.RustLsp('runnables')
end, { silent = true, buffer = bufnr })


vim.keymap.set("n", "K", function()
  vim.cmd.RustLsp({'hover', 'actions'})
end, { silent = true, buffer = bufnr })

vim.keymap.set("n", "J", function()
  vim.cmd.RustLsp('joinLines')
end, { silent = true, buffer = bufnr })
