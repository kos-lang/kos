" Vim syntax file
" Language:     Kos
" Maintainer:   Chris Dragan <chris@chris.dragan.name>
" Last Change:  2015 Jun 24

" For version 5.x: Clear all syntax items
" For version 6.x: Quit when a syntax file was already loaded
" tuning parameters:
" unlet kos_fold

if !exists("main_syntax")
  if version < 600
    syntax clear
  elseif exists("b:current_syntax")
    finish
  endif
  let main_syntax = 'kos'
endif

" Drop fold if it set but vim doesn't support it.
if version < 600 && exists("kos_fold")
  unlet kos_fold
endif


syn keyword kosCommentTodo      TODO FIXME XXX TBD contained
syn match   kosLineComment      "\/\/.*\|#.*" contains=@Spell,kosCommentTodo
syn region  kosComment          start="/\*"  end="\*/" contains=@Spell,kosCommentTodo
syn match   kosSpecial          "\\x[0-9a-fA-F][0-9a-fA-F]\|\\x{[0-9a-fA-F]\+}|\\."
syn region  kosStringD          start=+"+  skip=+\\\\\|\\"+  end=+"\|$+ contains=kosSpecial,kosStrFmt
syn region  kosStringS          start=+'+  skip=+\\\\\|\\'+  end=+'\|$+ contains=kosSpecial,kosStrFmt
syn region  kosStrFmt           start="\v\\\(\s*" end="\v\s*\)" contained containedin=kosStringD,kosStringS contains=kosStrInFmt,kosOperator,kosBoolean,kosNull,kosFunction,kosNumber,kosStringD,kosStringS,kosComment,kosTab
syn match   kosStrInFmt         "\v\w+(\(\))?" contained containedin=kosStrFmt
syn match   kosTab              "\t"

syn match   kosNumber           "\<\([1-9][0-9_]*\|0\)\(\.[0-9_]*\)\=\([eEpP][+-]\=[0-9_]\+\)\=\>\|0[bB][01_]\+\|0[xX][0-9a-fA-F_]\+\>"

syn keyword kosConditional      if else switch
syn keyword kosRepeat           while for repeat loop
syn keyword kosBranch           break continue fallthrough
syn keyword kosOperator         delete instanceof typeof yield in
syn keyword kosStatement        return with import try catch defer do
syn keyword kosBoolean          true false
syn keyword kosNull             void
syn keyword kosIdentifier       __line__ this var const
syn keyword kosException        throw assert
syn keyword kosReserved         set get only class static

if exists("kos_fold")
    syn match   kosFunction     "\<fun\>\|λ\|\<constructor\>"
    syn region  kosFunctionFold start="\(\<fun\>\|λ\|\<constructor\>\).*[^};]$" end="^\z1}.*$" transparent fold keepend

    syn sync match kosSync      grouphere kosFunctionFold "\<fun\>\|λ\|\<constructor\>"
    syn sync match kosSync      grouphere NONE "^}"

    setlocal foldmethod=syntax
    setlocal foldtext=getline(v:foldstart)
else
    syn keyword kosFunction     fun λ constructor
    syn match   kosBraces       "[{}\[\]]"
    syn match   kosParens       "[()]"
endif

syn sync fromstart
syn sync maxlines=100

if main_syntax == "kos"
  syn sync ccomment kosComment
endif

" Define the default highlighting.
" For version 5.7 and earlier: only when not done already
" For version 5.8 and later: only when an item doesn't have highlighting yet
if version >= 508 || !exists("did_kos_syn_inits")
  if version < 508
    let did_kos_syn_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif
  HiLink kosComment             Comment
  HiLink kosLineComment         Comment
  HiLink kosCommentTodo         Todo
  HiLink kosSpecial             Special
  HiLink kosStringS             String
  HiLink kosStringD             String
  HiLink kosNumber              Number
  HiLink kosConditional         Conditional
  HiLink kosRepeat              Repeat
  HiLink kosBranch              Conditional
  HiLink kosOperator            Operator
  HiLink kosStatement           Statement
  HiLink kosFunction            Function
  HiLink kosBraces              Function
  HiLink kosTab                 Error
  HiLink kosNull                Keyword
  HiLink kosBoolean             Boolean

  HiLink kosIdentifier          Identifier
  HiLink kosException           Exception
  HiLink kosReserved            Keyword

  delcommand HiLink
endif

let b:current_syntax = "kos"
if main_syntax == 'kos'
  unlet main_syntax
endif

" Tabs are not supported, expand them
setlocal et
