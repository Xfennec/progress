cv - Coreutils Viewer
=====================

What is it ?
------------

This tool can be described as a **Tiny** Dirty Linux Only C command that looks
for coreutils basic commands (cp, mv, dd, tar, gzip/gunzip, cat, ...) currently
running on your system and displays the percentage of copied data.

![cv screenshot with cp and mv](https://raw.github.com/Xfennec/cv/master/capture.png)

It's probably easy to add a progress bar, and with a bit more work, a "top-like"
mode with estimated time and data throughput.

How do you build it ?
---------------------

```
make && make install
```


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
watch cv -c firefox
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
