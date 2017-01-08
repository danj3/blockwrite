# blockwrite

A simple block device testing and performance tool.

Has options that defeat disk caches (by reading backwards).

The code pre-dates 2013 and has no documentation. However, if anyone comes across it and needs help, I'm very happy to add some.

## Command line options

```
Usage: blockwrite [OPTION...]
  -b, --blocksz=STRING     write block size
  -c, --count=INT          count of blocks to write
  -d, --datasz=STRING      data qty to write (scale sensitive)
  -f, --ffreq=INT          fsync frequency
  -F, --fsync-dset=INT     fsyncs per dataset
  -o, --output=STRING      output file or device
  -n, --name=STRING        Test name
  -C, --config=STRING      Config name (no spaces, colons)
  -p, --forks=INT          Parallel forks doing same work
  -K, --kb                 output KByte scaling
  -M, --mb                 output MByte scaling
  -G, --gb                 output GByte scaling
  -s, --stride=INT         Stride data to defeat rewrite caching
  -r, --read               read instead of write
  -R, --read-back          read backward, combine with -r
  -t, --trunc              truncate output file

Help options:
  -?, --help               Show this help message
      --usage              Display brief usage message
```

## Compilation

The Makefile is known good on OS X but requires linux popt (available via brew). On linux popt-dev is required.
