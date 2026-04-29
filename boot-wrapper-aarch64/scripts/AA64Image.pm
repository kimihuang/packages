#!/usr/bin/perl -w
# A simple AArch64 Linux Image header parser
#
# Copyright (C) 2021 ARM Limited. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE.txt file.

use warnings;
use strict;
use integer;

package AA64Image;

# Header definitions from v5.13
# See https://www.kernel.org/doc/html/v5.13/arm64/booting.html#call-the-kernel-image

use constant {
	HEADER_LEN => 64,
	HEADER_MAGIC => 0x644d5241,
};

sub parse
{
	my $class = shift;
	my $fh = shift;
	my $self = bless {}, $class;

	read($fh, my $raw_header, AA64Image::HEADER_LEN) == AA64Image::HEADER_LEN or goto failed;

	(
		$self->{code0},
		$self->{code1},
		$self->{text_offset},
		$self->{image_size},
		$self->{flags},
		$self->{res2},
		$self->{res3},
		$self->{res4},
		$self->{magic},
		$self->{res5}
	) = unpack("VVQ<Q<Q<Q<Q<Q<VV", $raw_header);

	if ($self->{magic} != AA64Image::HEADER_MAGIC) {
		warn "Image header magic not found";
		goto failed;
	}

	return $self;

failed:
	warn "Unable to parse header";
	return undef;
}

sub get_text_offset
{
	my $self = shift;

	# Where image_size is 0, the load offset can be assumed to be 0x80000
	# See https://www.kernel.org/doc/html/v5.13/arm64/booting.html#call-the-kernel-image
	if ($self->{image_size} == 0) {
		return 0x80000;
	}

	return $self->{text_offset};
}

sub get_load_offset
{
	my $self = shift;
	my $min = shift;
	my $offset = $self->get_text_offset();

	if ($min <= $offset) {
		return $offset;
	}

	# The image must be placed text_offset bytes from a 2MB aligned base address
	my $size_2m = 2 * 1024 * 1024;
	$min += $size_2m - 1;
	$min &= ~($size_2m - 1);

	return $min + $offset;
}

1;
