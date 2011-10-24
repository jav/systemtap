#!/bin/awk -f

BEGIN {
    isfunc = 0
    block = 0
    embedded = 0
}

# print the function prototype, all on one line.
/^function/ {
    if (block != 0 || embedded != 0) {
	printf "Unterminated block at: %s: %d %d\n",$0,block,embedded > "/dev/stderr"
	exit 1
    }
    isfunc = 1
    unprivileged = -1
    myproc_unprivileged = -1
}

/^function/,/\)/ {
    # Strip characters after the ')'.
    temp = $0
    sub("\\).*", ")", temp)
#    print "after stripping trailing chars: " temp

    # Strip the the return type.
    temp = gensub("^function[[:space:]]*([\\$_[:alpha:]][\\$_[:alnum:]]*)[[:space:]]*:[[:space:]]*[[:alpha:]]*",
		  "function \\1", 1, temp)
#    print "after stripping return type: " temp

    # Strip the 'function'.
    temp = gensub("^function[[:space:]]*", "", 1, temp)
#    print "after stripping function: " temp

    # Add the default type where it has been omitted.
    do {
	temp1 = temp
	temp = gensub("([(,][[:space:]]*[\\$_[:alpha:]][\\$_[:alnum:]]*)[[:space:]]*([,)])",
		      "\\1:long\\2", "g", temp)
#	print "after adding a default type: " temp
    } while (temp != temp1)

    # Strip paramater names.
    temp = gensub("[\\$_[:alpha:]][\\$_[:alnum:]]*[[:space:]]*:[[:space:]]*([[:alpha:]])",
		  "\\1", "g", temp)
#    print "after stripping parm names: "

    printf "%s",temp
    fflush()
}

# Beginning of a preprocessor block
(! /^.*\/\/.*%\(/) && /%\(/ {
    if (isfunc)
	block += 1
#    printf "%s",$0
#    print ": preprocessor start:" block " " embedded
}

# Beginning of an embedded C block
(! /^.*\/\/.*%{/) && /%{/ {
    block += 1
    embedded += 1
#    printf "%s",$0
#    print ": embedded start:" block " " embedded
}

# Beginning of a code block
/^{/ {
    block += 1
#    printf "%s",$0
#    print ": block start:" block " " embedded
}
(! /^.*\/\/.*{/) && /[^%]{/ {
    block += 1
#    printf "%s",$0
#    print ": block start:" block " " embedded
}

# unprivileged?
/\/\* unprivileged \*\// {
    if (embedded && unprivileged != 0)
	unprivileged = 1
}
/\/\* myproc-unprivileged \*\// {
    if (embedded && myproc_unprivileged != 0)
	myproc_unprivileged = 1
}

# Process the end of a tapset function.
function finish_function() {
    if (block == 0 && isfunc) {
	if (unprivileged == 1)
	    print ";unprivileged"
	else if (myproc_unprivileged == 1)
	    print ";myproc-unprivileged"
	else if (unprivileged != -1 || myproc_unprivileged != -1)
	    print ";privileged"
	else
	    print ";no embedded C"
	fflush()
	isfunc = 0
    }
}

# End of a preprocessor C block
(! /^.*\/\/.*%\)/) && /%\)/ {
    if (isfunc)
	block -= 1
#    printf "%s",$0
#    print ": embedded end:" block " " embedded
    finish_function()
}

# End of an embedded C block
(! /^.*\/\/.*%}/) && /%}/ {
    embedded -= 1
    block -= 1
    if (unprivileged == -1)
	unprivileged = 0
    if (myproc_unprivileged == -1)
	myproc_unprivileged = 0
#    printf "%s",$0
#    print ": embedded end:" block " " embedded
    finish_function()
}

# End of a code block
/^}/ {
    block -= 1
#   printf "%s",$0
#   print ": block end:" block " " embedded
    finish_function()
}
(! /^.*\/\/.*}/) && /[^%]}/ {
    block -= 1
#    printf "%s",$0
#    print ": block end:" block " " embedded
    finish_function()
}

END {
    if (block != 0 || embedded != 0) {
	printf "Unterminated block at EOF: %d %d\n",block,embedded > "/dev/stderr"
	exit 1
    }
}
