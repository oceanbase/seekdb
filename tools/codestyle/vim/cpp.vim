function! IndentNamespace()
  let l:cline_num = line('.')
  let l:pline_num = prevnonblank(l:cline_num - 1)
  let l:pline = getline(l:pline_num)
  let l:retv = cindent('.')
  while l:pline =~# '\(^\s*{\s*\|^\s*//\|^\s*/\*\|\*/\s*$\)'
    let l:pline_num = prevnonblank(l:pline_num - 1)
    let l:pline = getline(l:pline_num)
  endwhile
  if l:pline =~# '^\s*namespace.*'
    let l:retv = 0
  endif
  return l:retv
endfunction

setlocal shiftwidth=2
setlocal tabstop=2
setlocal softtabstop=2
setlocal expandtab
setlocal textwidth=100
setlocal nowrap

"setlocal cindent
"setlocal cinoptions=g0,h2,l2,t0,(0,w1,W4,f0,i4,+4
setlocal cinoptions=g0,N-s,h2,l2,t0,i4,+4,(0,w1,(0,W4
" setlocal cinoptions=g0:0,N-s,h1,l2,t0,i4,+4,(0,w1,W4
setlocal indentexpr=IndentNamespace()
