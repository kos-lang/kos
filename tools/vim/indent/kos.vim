" Vim indent file
" Language:     Kos
" Maintainer:   Chris Dragan <chris@chris.dragan.name>
" Last Change:  2015 Jun 24

" Only load this indent file when no other was loaded.
if exists("b:did_indent")
   finish
endif
let b:did_indent = 1

" C indenting is not too bad.
setlocal cindent
setlocal cinkeys-=0#
setlocal cinoptions+=j1,J1,#1

let b:undo_indent = "setl cin<"
