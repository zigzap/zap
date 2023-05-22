import sys
import os
from git import Repo
import subprocess

TAG_NAME = os.getenv("TAG_NAME", sys.argv[1])


def get_tag_annotation(tagname):
    repo = Repo('.')
    tag = repo.tags[tagname]
    return tag.tag.message


def get_replacement():
    ret = subprocess.run([
        "./zig-out/bin/pkghash",
        "-g", f"--tag={TAG_NAME}",
        "--template=./tools/announceybot/release-note-template.md",
        ], capture_output=True)
    text = ret.stdout.decode("utf-8")
    return text


if __name__ == '__main__':
    annotation = get_tag_annotation(TAG_NAME)
    print(get_replacement().replace("{{UPDATES}}", annotation))
