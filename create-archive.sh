#!/usr/bin/env bash
tag=$1
if [ "$tag" == "" ] ; then 
    echo provide tag
    exit 1
fi
git archive --format=tar.gz -o ${tag}.tar.gz --prefix=zap-$tag/ HEAD
