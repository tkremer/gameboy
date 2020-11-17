#!/usr/bin/perl

use strict;
use warnings;

sub crc8 {
  my $data = shift;
  my $crc = 0;
  my $poly = 0x8c;
  for (split //, $data) {
    my $c = ord($_);
    $crc ^= $c;
    for (0..7) {
      #my $bit = ($c ^ $crc) & 1;
      my $bit = $crc & 1;
      #$c >>= 1;
      $crc >>= 1;
      if ($bit) {
        $crc ^= $poly;
      }
    }
  }
  return chr($crc);
}

while (<>) {
  chomp;
  my $data = pack("H*",$_);
  my $res = crc8($data);
  print unpack("H*", $res),"\n";
}
