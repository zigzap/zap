# <!-- INSERT_DEP_BEGIN -->
# ```zig 
# .{
#     .name = "myapp",
#     .version = "0.0.1",
#     .dependencies = .{
#         // zap release-0.0.24
#         .zap = .{
#             .url = "https://github.com/zigzap/zap/archive/refs/tags/release-0.0.24.tar.gz",
#             .hash = "1220f520fcdd4b3adbca918deeb42f512f7ef4a827680eea8af9abc64b90ed7a5e78",
#         }
#     }
#
# }
# ```
# <!-- INSERT_DEP_END -->

import os
import sys
import subprocess

TAG_NAME = os.getenv('TAG_NAME', sys.argv[1])

REPLACE_BEGIN_MARKER = '<!-- INSERT_DEP_BEGIN -->'
REPLACE_END_MARKER = '<!-- INSERT_DEP_END -->'


def get_replacement():
    ret = subprocess.run([
        "./zig-out/bin/pkghash",
        "-g", f"--tag={TAG_NAME}",
        "--template=./tools/announceybot/release-dep-update-template.md",
        ], capture_output=True)
    text = ret.stdout.decode("utf-8")
    return text

out_lines = []
with open('README.md', 'rt') as f:
    in_replace_block = False
    update_lines = get_replacement().split("\n")

    print("Updating with:")
    print('\n'.join(update_lines))

    lines = [l.rstrip() for l in f.readlines()]
    for line in lines:
        if in_replace_block:
            if line.startswith(REPLACE_END_MARKER):
                in_replace_block = False
            continue
            # ignore the line
        if line.startswith(REPLACE_BEGIN_MARKER):
            out_lines.append(REPLACE_BEGIN_MARKER)
            in_replace_block = True
            # append the stuff
            out_lines.extend(update_lines)
            out_lines.append(REPLACE_END_MARKER)
            continue
        out_lines.append(line)

with open('README.md', 'wt') as f:
    f.write('\n'.join(out_lines))
