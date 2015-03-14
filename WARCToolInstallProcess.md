# Requirements #

WARC is a self-contained library with no requirements but GCC.


The library was tested in x86 and PPC on both 32 bits and 64 bits architectures.

  * **Linux**:

> GCC >= 3.x.y

  * **Windows MinGW**:

> Install the following items in this order:
```
    http://kent.dl.sourceforge.net/sourceforge/mingw/MinGW-5.1.3.exe
    http://kent.dl.sourceforge.net/sourceforge/mingw/MSYS-1.0.11-2004.04.30-1.exe
```

  * **Windows Cygwin**:

> Install the following application:
```
    http://cygwin.com/setup.exe
```

  * **MacOS X** (Intel or PPC):

> Install [XCode](http://developer.apple.com/tools/xcode/)

  * **FreeBSD/NetBSD/OpenBSD**:

> Use "gmake" instead of "make" when compiling.

  * **Solaris**:

> First, go to "sunfreeware.com" and downloaded "gcc", "libiconv" and the
> "make" packages for your Solaris achitecture (i.e. SPARC/X86) on
> the right panel.

> We assume here that you're using a Solaris 10 on a SPARC architecture. As root type:
```
    # wget ftp://ftp.sunfreeware.com/pub/freeware/sparc/10/libiconv-1.11-sol10-sparc-local.gz
    # wget ftp://ftp.sunfreeware.com/pub/freeware/sparc/10/gcc-3.4.6-sol10-sparc-local.gz
    # wget ftp://ftp.sunfreeware.com/pub/freeware/sparc/10/make-3.81-sol10-sparc-local.gz

    # gunzip libiconv-1.11-sol10-sparc-local.gz
    # gunzip gcc-3.4.6-sol10-sparc-local.gz
    # gunzip make-3.81-sol10-sparc-local.gz
    # gunzip binutils-2.17-sol10-sparc-local.gz

    # pkgadd -d libiconv-1.11-sol10-sparc-local
    # pkgadd -d gcc-3.4.6-sol10-sparc-local
    # pkgadd -d make-3.81-sol10-sparc-local
    # pkgadd -d binutils-2.17-sol10-sparc-local

    # export PATH=$PATH:/usr/local/bin
```


# Install #

Get a fresh copy of the library from subversion:

```
   svn checkout http://warc-tools.googlecode.com/svn/trunk/ warc-tools-read-only
   cd warc-tools-read-only
```

Compile the library with:

```
   make
```

or use _gmake_ on FreeBSD, NetBSD or OpenBSD:

```
   gmake
```

# see doc/install for more information #