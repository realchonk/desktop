unlet! skip_defaults_vim
source $VIMRUNTIME/defaults.vim

set nu
set relativenumber
set mouse=
set ruler
set clipboard^=unnamedplus
set smartindent
autocmd VimLeave * call system("echo -n $'" . escape(getreg(), "'") . "' | xclip -sel clipboard")

filetype plugin on
filetype indent on
syntax on
