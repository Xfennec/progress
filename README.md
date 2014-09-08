cv - Coreutils Viewer
=====================

What is it ?
------------

This tool can be described as a **Tiny** Dirty Linux Only* C command that looks
for coreutils basic commands (cp, mv, dd, tar, gzip/gunzip, cat, ...) currently
running on your system and displays the percentage of copied data.

It can now also display an estimated throughput (using -w flag).

![cv screenshot with cp and mv](https://raw.github.com/Xfennec/cv/master/capture.png)

_(After many requests: the colors in the shell comes from [powerline-shell](https://github.com/milkbikis/powerline-shell). Try it, it's cool.)_

It's probably easy to add a progress, show estimated time, and with a bit more work,
provide a "top-like" mode with more accurate informations.

* Note: a [Mac OSX port is available](https://github.com/BestPig/cv) until we
merge the support upstream.

How do you build it ?
---------------------

```
make && make install
```

It depends on ncurses library, you may have to install corresponding packages.

How do you run it ?
-------------------

Just launch the binary, « cv ».


What can I do with it ?
-----------------------

A few examples. You can …

… monitor all current and upcoming instances of coreutils commands in
a simple window:
```
watch cv -q
```

… see how your download is running:
```
watch cv -wc firefox
```

… look at your Web server activity:
```
cv -c httpd
```

And many more.

How does it work ?
------------------

It simply scans /proc for interesting commands, and then use fd/ and fdinfo/
directories to find opened files and seek position, and reports status for
the biggest file.

It's very light, and compatible with virtually any command.
