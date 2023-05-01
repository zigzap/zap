#!/usr/bin/env bash
tag=$1
override=$2

if [ "$tag" == "--override" ] ; then 
    override=$tag
    tag=""
fi

if [ "$tag" == "" ] ; then 
    tag=$(git rev-parse --abbrev-ref HEAD)
    echo "Warning: no tag provided, using: >> $tag <<"
fi


git archive --format=tar.gz -o ${tag}.tar.gz --prefix=zap-$tag/ HEAD

git diff --quiet 

if [ $? -ne 0 ] ; then
    if [ "$override" == "--override" ] ; then
        ./zig-out/bin/pkghash -g --tag=$tag --template=doc/release-template.md
    else
        echo "WARNING: GIT WORKING TREE IS DIRTY!"
        echo "If you want to get zig hash anyway, run:"
        echo "./zig-out/bin/pkghash -g"
        echo "or, with full-blown release-notes:"
        echo "./zig-out/bin/pkghash -g --tag=$tag --template=doc/release-template.md"
        echo ""
        echo "To skip this message and do the pkghash thing anyway, supply the"
        echo "--override parameter"
    fi
else
    ./zig-out/bin/pkghash -g --tag=$tag --template=doc/release-template.md
fi
