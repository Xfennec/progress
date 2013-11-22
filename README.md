cv - Coreutils Viewer
=====================

What is it ?
------------

This tool can be described as a **Tiny** Dirty Linux Only C command that looks
for coreutils basic commands (currently cp, mv, dd and cat) currently running
on your system and displays the percentage of copied data.

It's probably easy to add a progress bar, and with a bit more work, a "top-like"
mode with estimated time and data throughput.

How do you build it ?
---------------------

```
make && make install
```

How do you run it ?
-------------------

Just launch the binary, « cv ». You can also combine this with watch :
```
watch cv
```

How does it work ?
------------------

It simply scans /proc for interesting commands (like cp, for instance), and
then use fd/ and fdinfo/ directories to find opened files and seek position,
and that's it.

It's very light, and compatible with virtually any command.
