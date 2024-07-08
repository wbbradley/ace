autocmd FileType cpp nnoremap <F4> :wa<CR> :e %:p:s,.h$,.X123X,:s,.cpp$,.h,:s,.X123X$,.cpp,<CR>
autocmd FileType cpp inoremap <F4> <Esc> <F4>
autocmd FileType cpp vnoremap <F4> <Esc> <F4>
autocmd FileType c nnoremap <F4> :wa<CR> :e %:p:s,.h$,.X123X,:s,.cpp$,.h,:s,.X123X$,.cpp,<CR>
autocmd FileType c inoremap <F4> <Esc> <F4>
autocmd FileType c vnoremap <F4> <Esc> <F4>
nnoremap <Leader>` :echom system("./cider-tags\ .")<CR>
autocmd FileType * setlocal makeprg=make\ debug

let &path=substitute(
      \ system('pkg-config bdw-gc --variable=includedir'),
      \ '\n', '', 'g') . ',.'
let g:ale_cpp_clang_options=substitute(system("pkg-config bdw-gc --cflags") . ' ' . 
      \ system("llvm-config --cxxflags"), '\n', '', 'g')
let g:ale_cpp_cc_options=substitute(system("pkg-config bdw-gc --cflags")
      \ . ' -isysroot ' . system("xcrun --sdk macosx --show-sdk-path")
      \ . ' ' .  system("llvm-config --cxxflags"), '\n', '', 'g')
let g:ale_c_clang_options=substitute(system("pkg-config bdw-gc --cflags") . ' ' . 
      \ system("llvm-config --cflags"), '\n', '', 'g')
