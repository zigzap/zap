#!/bin/sh

# this file is installed in the repository as file: .git/hooks/pre-commit

# we check for `./src/deps/facilio` in the list of files to be committed and 
# only allow to proceed if the IKNOWWHATIMDOING env var is set

if git rev-parse --verify HEAD >/dev/null 2>&1
then
	against=HEAD
else
	# Initial commit: diff against an empty tree object
	against=$(git hash-object -t tree /dev/null)
fi

# Redirect output to stderr.
exec 1>&2


if git diff --name-only --cached --diff-filter=M | grep 'src/deps/facilio' > /dev/null; then
    if [ "$IKNOWWHATIMDOING" = "" ] ; then
	cat <<\EOF
Error: src/deps/facilio is staged for commit. 

This is most likely unintentional. Pease consider if this is what you want. 

See `CONTRIBUTING.md` for an explaination.

If you know what you are doing you can disable this check using:

    IKNOWWHATIMDOING=true git commit ...
EOF
    exit 1
    fi
    echo "WARNING: src/deps/facilio is staged for commit. See CONTRIBUTING.md"
fi

