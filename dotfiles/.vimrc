unlet! skip_defaults_vim
source $VIMRUNTIME/defaults.vim

set nu
set relativenumber
set mouse=
set ruler
set clipboard^=unnamedplus
set smartindent
autocmd VimLeave * call system("echo -n '" . escape(getreg(), "'") . "' | xclip -sel clipboard")

filetype plugin on
filetype indent on
syntax on

map <F1> <Esc>
imap <F1> <Esc>

if $TERM == 'xterm'
	set t_TI=^[[4?h
	set t_TE=^[[4?l
endif


