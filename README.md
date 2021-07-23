progress - Coreutils Progress Viewer [![Build Status](https://travis-ci.org/Xfennec/progress.svg?branch=master)](https://travis-ci.org/Xfennec/progress)
=====================

What is it
----------

This tool can be described as a **Tiny**, Dirty C command
that looks for coreutils basic commands (cp, mv, dd, tar, gzip/gunzip,
cat, etc.) currently running on your system and displays the
**percentage** of copied data. It can also show **estimated time** and **throughput**,
and provides a "top-like" mode (monitoring).

![progress screenshot with cp and mv](https://raw.github.com/Xfennec/progress/master/capture.png)

_(After many requests: the colors in the shell come from [powerline-shell](https://github.com/milkbikis/powerline-shell). Try it, it's cool.)_

`progress` works on Linux, FreeBSD and macOS.

Formerly known as cv (Coreutils Viewer).

How do you install it
---------------------

On deb-based systems (Debian, Ubuntu, Mint, etc.) run:

    apt install progress

On archlinux, run:

    pacman -S progress

On rpm-based systems (Red Hat, CentOS, Fedora, SUSE, etc.), run one of these:

    dnf install progress
    yum install progress

On macOS, with homebrew, run:

    brew install progress

On macOS, with MacPorts, run:

    port install progress

How do you build it from source
-------------------------------

    make && make install

On FreeBSD, substitute `make` with `gmake`.

It depends on library ncurses, you may have to install corresponding packages (may be something like 'libncurses5-dev', 'libncursesw6' or 'ncurses-devel').

How do you run it
-----------------

Just launch the binary, `progress`.

What can I do with it
---------------------

A few examples. You can:

* monitor all current and upcoming instances of coreutils commands in
  a simple window:

        watch progress -q

* see how your download is progressing:

        watch progress -wc firefox

* look at your Web server activity:

        progress -c httpd

* launch and monitor any heavy command using `$!`:

        cp bigfile newfile & progress -mp $!

and much more.

How does it work
----------------

It simply scans `/proc` for interesting commands, and then looks at
directories `fd` and `fdinfo` to find opened files and seek positions,
and reports status for the largest file.

It's very light, and compatible with virtually any command.
