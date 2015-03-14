# Introduction #

The following command-line specs are intended as a proof of concept to illustrate how you can build on the WARC standard enabled by libwarc. Feel free to add your own command-lines, as a wish list or otherwise.

Two important functions of these command-lines are:
  1. a specification of use-cases
  1. a test case specification

So if you contribute command-lines to this list, please add usage notes on how it may be used and how it may help test libwarc.

# Command-Line Specifications #

## warcdump (done) ##
SRS 11-14, 27-28

Dump a WARC file

Usage: ./app/warcdump -f <file.warc> [-c] [-v]
  * -f    : valid WARC file name
  * [-c]  : GZIP compressed WARC (default true)
  * [-v]  : dump ANVL (default false)

## warcfilter (done) ##
SRS 16-20

Filter WARC records based on MIME or URI

Usage: ./app/warcfilter -f <file.warc> [-c] -s 

&lt;match&gt;

 [-t] [-v]
  * -f    : valid WARC file name
  * [-c]  : GZIP compressed WARC (default true)
  * -s    : pattern string
  * [-t]  : compare with MIME (default true)
  * [-v]  : dump ANVL (default false)\n"

## warcappend ##
SRS 8-10

This command can easily be built with only few instructions using the WARC library.


## warcvalidator (done) ##
SRS 31-32

Check WARC file consistency

Usage: ./app/warcvalidator -f <file.warc> [-c] [-v]
  * -f    : valid WARC file name
  * [-c]  : GZIP compressed WARC (default true)
  * [-v]  : verbose mode (default true)


## warcvalidator.sh (done) ##

Check if all WARC files in a directory are valid

Usage: app/warcvalidator.sh <-d dirname> [-c wgzip] [-v] [-h]
  * -d     : directory name containing ARC files
  * -c     : WARC files are compressed (default yes)
  * -h     : print this help message
  * -v     : output version information and exit


## arc2warc (done) ##
SRS 11-14, 15, 44

ARC to WARC convertor

Usage: ./app/arc2warc -a <file.arc> [-b] -f <file.warc> [-c]
  * -a    : valid ARC file name
  * [-b]  : GZIP compressed ARC (default true)
  * -f    : valid WARC file name
  * [-c]  : GZIP compressed WARC (default true)


## arc2warc.sh (done) ##
SRS 31-32

Converts all ARC files in a directory to WARC files

Usage: ./app/arc2warc.sh <-d dirname> [-b agzip] [-c wgzip] [-v] [-h]
  * -d     : directory name containing ARC files
  * -b     : ARC files are compressed (default yes)
  * -c     : WARC files will be compressed (default yes)
  * -h     : print this help message
  * -v     : output version information and exit


## warcserver ##
SRS 33-36, 37-38

`warcserver -d [-r <warcs_dir>]`

where:
  * -d: run the wserver as a daemon
  * -r: WARCs repository base path

## mkwarc ##

Wraps the given input file or files into a WARC file with one "resource" entry per input file.

`mkwarc [-c] -i <file1> [<file2> [<file3>...]]`

where:
  * -i: Input file(s) (required)
  * -c: Compress the WARC files (default no)
  * -m: Optional MIME type of file (default application/octet-stream)
  * -u: Optional URL of file (default file:://

&lt;filename&gt;

)

[I'm not sure how you would supply multiple URLs if there were multiple files. Perhaps the answer is to restrict this command to wrapping a single file into WARC< and to then implement the warcappend command above to let users build up larger WARC files.]