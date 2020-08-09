autocmd FileType cpp nnoremap <F4> :wa<CR> :e %:p:s,.h$,.X123X,:s,.cpp$,.h,:s,.X123X$,.cpp,<CR>
autocmd FileType cpp inoremap <F4> <Esc> <F4>
autocmd FileType cpp vnoremap <F4> <Esc> <F4>
autocmd FileType c nnoremap <F4> :wa<CR> :e %:p:s,.h$,.X123X,:s,.cpp$,.h,:s,.X123X$,.cpp,<CR>
autocmd FileType c inoremap <F4> <Esc> <F4>
autocmd FileType c vnoremap <F4> <Esc> <F4>
nnoremap <Leader>` :echom system("./zion-tags\ .")<CR>
autocmd FileType * setlocal makeprg=make\ debug
let &path='src/vendor,' . substitute(
      \ system('pkg-config bdw-gc --variable=includedir'),
      \ '\n', '', 'g') . ',.'
let g:ale_cpp_clang_options='--std=c++17 -Isrc/vendor ' . substitute(system("pkg-config bdw-gc --cflags") . ' ' . 
      \ system("llvm-config --cxxflags"), '\n', '', 'g')
let g:ale_c_clang_options='-Isrc/vendor ' . substitute(system("pkg-config bdw-gc --cflags") . ' ' . 
      \ system("llvm-config --cflags"), '\n', '', 'g')
