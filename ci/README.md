# ci

`Dockerfile.tl4-build` is the build image of the TL4 CI (`tl4-build:2` on libernt-srv and on the Lab01 runner VM): clang 18 with the sanitizer runtime, gcc with the i386 multilib, cmake and ninja; it holds no repository code and the job runs it with the source mounted read-only, no network and an unprivileged user.
The base image is pinned to the `ubuntu:24.04` index digest it was built from on 2026-11-15 (image created 2026-11-10), so the runner builds the same image from a given commit; move the digest in its own commit when the base is updated.
Build: `docker build -f ci/Dockerfile.tl4-build -t tl4-build:2 ci` (the context holds only this file).
