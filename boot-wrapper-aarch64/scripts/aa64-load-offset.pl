#!/usr/bin/perl -w
# Find the load address of an AArch64 Linux Image
#
# Usage: ./$0 <Image> <min-offset>
#
# Copyright (C) 2021 ARM Limited. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.txt file.

use warnings;
use strict;

use AA64Image;

my $filename = shift;
die("No filename provided") unless defined($filename);

my $min = shift;
$min = oct($min) if $min =~ /^0/;

open (my $fh, "<:raw", $filename) or die("Unable to open file '$filename'");

my $image = AA64Image->parse($fh) or die("Unable to parse Image");

my $offset = $image->get_load_offset($min);

printf("0x%016x\n", $offset);
