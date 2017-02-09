#!/usr/bin/perl

# usage:
#   pgm2font <name> <width(s)> <input.pgm >output.c
#
#   reads the white-on-black file as character bitmaps, <width(s)> gives the
#   width of each character (as a comma-separated list or a single common value)
#   <name> is the name of the font symbol generated.
#   reads the pgm file from stdin and writes the c file to stdout.

use strict;
use warnings;

use List::Util qw(sum);
use POSIX qw(floor ceil);

my $name = shift//die "need font name";
my $width = shift//die "need character width(s)".
my @widths;

my $pgm = {};
my $lineno = 0;
while(<>) {
  if ($lineno == 0) {
    die "invalid file" unless /^P5\r?$/;
    $lineno++;
  } elsif (/^#/) {
    next;
  } elsif ($lineno == 1) {
    if (/^(\d+) (\d+)\r?$/) {
      @$pgm{qw(w h)} = ($1+0,$2+0);
    } else {
      die "invalid line $lineno: \"$_\"";
    }
    $lineno++;
  } elsif ($lineno == 2) {
    die "invalid line $lineno: \"$_\"" unless /^255\r?$/;
    $lineno++;
    last;
  }
}
die "unexpected end of file" unless $lineno == 3;
{
  my $size = $pgm->{w}*$pgm->{h};
  my $res = read(STDIN,my $buf, $size);
  die "short read" unless $res == $size && length($buf) == $size;
  $pgm->{data} = $buf;
}

if ($width =~ /,/) {
  @widths = split /,/,$width;
} else {
  my $symbols = floor($pgm->{w}/$width);
  @widths = ($width)x$symbols;
}
my $symbols = @widths;
my $total_w = sum(@widths);
die "short image" unless $total_w <= $pgm->{w};

my (@data,@offsets);

my $h = $pgm->{h};
my $h0 = ceil($h/8);
my $x0 = 0;
for my $i (0..$symbols-1) {
  my $w = $widths[$i];
  my @char;
  for my $y (0..$h0-1) {
    for my $x (0..$w-1) {
      my $val = 0;
      for my $bit (0..7) {
        next if $bit >= $h-$y*8;
        my $value = ord(substr($pgm->{data},$x0+$x+$pgm->{w}*($y*8+$bit)));
        if ($value) {
          $val |= 1<<$bit;
        }
      }
      push @char, $val;
    }
  }
  push @data, \@char;
  $x0 += $w;
  push @offsets, $x0*$h0;
}

my $codepoint = 0x20;
for (@data) {
  my $char = chr($codepoint);
  $char = "\\-" if $char eq "\\";
  $_ = sprintf "0x%02x",$_ for @$_;
  $_ = [join(", ", @$_),"  // ".$char];
  $codepoint++;
}
my $lastdata = pop @data;
@data = (map(join(",",@$_), @data),join(" ",@$lastdata));

print "const uint16_t ${name}_offsets[$symbols] PROGMEM = {\n  ".join(", ",@offsets)."\n};\n";
print "const uint8_t ${name}_chars[$total_w] PROGMEM = {\n  ".join("\n  ",@data)."\n};\n";
print "const font_t ${name} PROGMEM = {\n  .height = $h,\n  .symbols = $symbols,\n  .offsets = ${name}_offsets,\n  .chars_black = ${name}_chars,\n  .chars_white = ${name}_chars\n};\n";

