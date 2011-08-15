" Vim support file to detect file types
" Language:	SystemTap
" Maintainer:	SystemTap Developers <systemtap@sourceware.org>
" Last Change:	2011 Aug 4
" Note: this overrides the default *.stp mapping to "Stored Procedures"
"   It would be nice to find a way to intelligently detect this.

" SystemTap scripts
au BufNewFile,BufRead *.stp			set ft=stap

