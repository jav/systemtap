" Vim support file to detect file types
" Language:	SystemTap
" Maintainer:	Josh Stone <joshua.i.stone@intel.com>
" Last Change:	2005 Dec 16
" Note: this overrides the default *.stp mapping to "Stored Procedures"
"   It would be nice to find a way to intelligently detect this.

" SystemTap scripts
au BufNewFile,BufRead *.stp			setf stap

