# Contributing to ZAP

Contributions are welcome! 🙏

## Communicating

I strongly favor [SourceHut](https://sr.ht)'s workflows, so I'll probably set up
a mailing list for discussions and patches. You can reach me
[there](https://sr.ht/~renerocksai) via
[e-mail](~renerocksai/public-inbox@lists.sr.ht).

A whole discord server as another option, while a funny idea, seems like a bit
of an overkill. You can reach me on [the zig showtime discord
server](https://discord.gg/CBzE3VMb) under the handle renerocksai
(renerocksai#1894).

Pull-requests and issues are, of course, welcome, too - and may be, for the time
being, the most both sane and GitHub-friendly way of communicating.

## The git pre-commit hook

This hook is checking for src/deps/facilio in the file list that
only lets you go through with the operation if a specific env var is set

why is that? we don't use a fork of facilio. we use their repo as submodule. on
build, we apply a patch to the submodule. after applying, the submodule is
dirty. it refers to a new head only present on the current machine. if we were
to commit the submodule now and push those changes, future git submodule update
--init calls will fail on fresh clones of the repo. We want to avoid this
mistake that easily happens if you use a `git add . && git commit` workflow. On
the other hand, if upstream facilio changes, we still want to be able to upgrade
to it by pulling the changes and recording the new head via git commit to our
own repo. Hence, just ignoring the submodule via `.gitignore` is not an option.
That's why we introduced the hook (// that gets installed on build.)
