#!/usr/bin/perl
# Adds the PDF-Betrachter icon to a geode built by the pcgeos-1-sdk-v2
# ensemble12 backend: app object gets a moniker list (text + 6 bitmaps),
# the bitmaps live in six extra LMem resources (5..10), laid out like the
# icon of the working "Schnecke" build.
# usage: add_icon.pl in.geo out.geo icon_dir
use strict; use warnings;
use FindBin; use lib (($ENV{SDK} || "../pcgeos-1-sdk-v2") . "/tools/lib");
use PCGEOS::Geos1::Format qw(from_bytes);
use PCGEOS::Geos1::Writer qw(build_geode);
use PCGEOS::Ensemble12::Objects qw(object_block text_moniker);
my ($in,$out,$dir)=@ARGV; die "usage\n" unless $dir;
sub slurp {open my $f,'<:raw',$_[0] or die "$_[0]: $!"; local $/; <$f>}
my $g=from_bytes(slurp($in),$in);
my @res=map {{data=>$_->{data},flags=>$_->{flags},relocations=>[map {[$_->{info},$_->{extra},$_->{offset}]} @{$_->{relocations}}]}} @{$g->{resources}};
die "expected the 5 ensemble12 resources\n" unless @res==5;

# --- application block (resource 4) with moniker list at chunk 0x2a ---
my $title=$g->{long_name}; my $token=pack('H*',substr($g->{token_hex},0,8));
my $app=pack('v14',0x3000,71,0,8,0,0,0,0,0x24,0x1003,0x2a,0,0,0x4000);
$app.="\0" x (198-length($app));
substr($app,28,2)=pack('v',0x28); substr($app,30,4)=$token; substr($app,197,1)=pack('C',0x80);
my @types=([5,0x20c6],[6,0x20c1],[7,0x10c6],[8,0x10c1],[9,0x20e1],[10,0x10e1]);
my $list=pack('vvv',0x1081,0x26,0x4000);
$list.=pack('vvv',$_->[1],0x22,0x1000|$_->[0]) for @types;
my $block=object_block(4,{0x24=>[$app,3],0x26=>[text_moniker($title),2],0x28=>[pack('v4',0x24,0x1003,0,0),2],0x2a=>[$list,2]},6);
$res[4]{data}=$block;

# --- icon resources ---
for my $t (@types) {
    my ($n)=@$t; my $mon=slurp("$dir/mon_$n.bin");
    my $b=object_block($n,{0x22=>[$mon,2]},2);
    substr($b,4,2)=pack('v',0xc000);           # LMem flags as in GEOS icon blocks
    push @res,{data=>$b,flags=>0x1c7a,relocations=>[[0x23,0,0]]};
}
my $new=build_geode(name=>$g->{name},extension=>$g->{extension},long_name=>$title,token=>$token,
    attributes=>$g->{attributes},file_type=>1,release=>$g->{release},protocol=>$g->{protocol},
    kernel_protocol=>$g->{kernel_protocol},udata_size=>$g->{udata_size},resources=>\@res,
    imports=>[map {{name=>$_->{name},attributes=>$_->{attributes},protocol=>$_->{protocol}}} @{$g->{imports}}],
    process_class=>[1,0x40],application_object=>[4,0x24]);
open my $f,'>:raw',$out or die; print $f $new->{raw}; close $f;
printf "%s: %d bytes, %d resources\n",$out,length($new->{raw}),scalar @res;
