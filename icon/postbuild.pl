#!/usr/bin/perl
# Post-build step for the PDF-Betrachter (pcgeos-1-sdk-v2, ensemble12 backend):
#  * menu "Datei" (GenInteraction + GenTriggers) in the interface block (res 3),
#    built like the menus of the GeoWorks 1.28 Notizblock (NOTEPAD.GEO)
#  * application icon (moniker list + six bitmap resources), like Schnecke
# usage: postbuild.pl in.geo out.geo icon_dir width height
use strict; use warnings;
use lib (($ENV{SDK} || "../pcgeos-1-sdk-v2") . "/tools/lib");
use PCGEOS::Geos1::Format qw(from_bytes);
use PCGEOS::Geos1::Writer qw(build_geode);
use PCGEOS::Ensemble12::Objects qw(object_block text_moniker SAME_BLOCK);
my ($in,$out,$dir,$w,$h)=@ARGV; die "usage\n" unless $h;
sub slurp {open my $f,'<:raw',$_[0] or die "$_[0]: $!"; local $/; <$f>}
sub set16 {my ($r,$at,$v)=@_; substr($$r,$at,2)=pack('v',$v)}
sub base {my ($ord,%o)=@_; pack('v14',0x3000,$ord,0,8,0,0,@{$o{sibling}//[0,0]},@{$o{child}//[0,0]},$o{moniker}//0,$o{accel}//0,$o{hints}//0,$o{states}//0xc000)}
sub moniker {my ($text,$mn)=@_; pack('vvCC',1,0,0,$mn//0xff).$text."\0"}
my $g=from_bytes(slurp($in),$in);
my @res=map {{data=>$_->{data},flags=>$_->{flags},relocations=>[map {[$_->{info},$_->{extra},$_->{offset}]} @{$_->{relocations}}]}} @{$g->{resources}};
die "expected the 5 ensemble12 resources\n" unless @res==5;
# the app keeps its variables from DGROUP offset 0xA0 on: the method table
# (0x52 + 6 bytes per method) must end below that
my $nm=ord(substr($res[1]{data},0x46,1));
die "too many methods ($nm): method table would overlap the variables at 0xA0\n" if 0x52+6*$nm>0xA0;
my $title=$g->{long_name}; my $token=pack('H*',substr($g->{token_hex},0,8));

# ---- interface block (resource 3): primary, title, view as the SDK builds them ----
my ($primary_h,$title_h,$view_h,$app_h)=(0x24,0x26,0x28,0x24);
my $primary=base(88,sibling=>[$app_h|1,0x1004],child=>[$view_h,SAME_BLOCK],moniker=>$title_h)."\0" x 5;
set16(\$primary,0x1c,0x22); set16(\$primary,0x1e,0x80);
my $mkview=sub {my $sib=shift; my $v=base(75,sibling=>$sib); $v.="\0" x (71-length($v));
  my %f=(0x20=>1,0x24=>1,0x2a=>15,0x2e=>0x8000,0x32=>$w,0x34=>$h,0x36=>$w,0x38=>$h,0x3a=>10,0x3c=>10,0x3e=>20,0x40=>15,0x44=>0x1000);
  set16(\$v,$_,$f{$_}) for keys %f; substr($v,0x46,1)=pack('C',1); $v};
my $check=object_block(3,{$primary_h=>[$primary,3],$title_h=>[text_moniker($title),2],$view_h=>[$mkview->([$primary_h|1,SAME_BLOCK]),7]},6);
die "interface block differs from the SDK build - layout assumption broken\n" unless $check eq $res[3]{data};

# ---- menus "Datei" and "Hilfe" ----
# Datei: MOTIF.GEO maps hint 0x4047 to "group belongs in the file menu" (as
# 0x4049 does for the window menu of GeoManager). Our group (Oeffnen,
# Schliessen, Info) and the PrintControl of the spool library (import 2,
# Drucken..., Faxen...) both carry it; Motif builds one "Datei" menu from
# them and adds Verlassen. Hilfe: own menu.
# GEOS 1 character set: 0x85 = O-umlaut, 0xA7 = sharp s
my %chunks=($primary_h=>[$primary,3],$title_h=>[text_moniker($title),2]);
my ($fm,$fm_m,$fm_hints)=(0x2a,0x2c,0x2e);
my @items=(["\x85ffnen",0x0c00],["Schlie\xa7en",0x0c01],['Info',0x0c03]);
my ($pc_h,$pc_hints)=(0x3c,0x3e);
my ($hm,$hm_m,$hm_hints,$ht,$ht_m)=(0x42,0x44,0x46,0x48,0x4a);
$chunks{$view_h}=[$mkview->([$fm,SAME_BLOCK]),7];
$chunks{$fm}=[base(80,sibling=>[$pc_h,SAME_BLOCK],child=>[0x30,SAME_BLOCK],hints=>$fm_hints).pack('C',2),3];
$chunks{$fm_m}=["\0",2]; $chunks{$fm_hints}=[pack('v8',0x4005,4,0x4047,4,0x4006,4,0x4007,4),2];
for my $i (0..$#items) {
  my ($label,$msg)=@{$items[$i]}; my $th=0x30+4*$i; my $mh=$th+2;
  my $sib=$i<$#items ? [$th+4,SAME_BLOCK] : [$fm|1,SAME_BLOCK];
  $chunks{$th}=[base(68,sibling=>$sib,moniker=>$mh,states=>0xc004).pack('vvv',0,0x1000,$msg),7];   # output = own process
  $chunks{$mh}=[moniker($label,0),2];
}
# PrintControl like the one of the Notizblock; attributes 0x2c (bit 0x20 =
# "Drucken..." with dialog); it sends 0x0c02 with the print GString to us
my ($em,$em_m,$em_hints)=(0x4e,0x50,0x52);
my $pc=pack('v5',0x3002,1,0,0x0a,0x1f).pack('v10',0,0,$em,SAME_BLOCK,0,0,0,0,$pc_hints,0xc000).pack('C',2)
      .pack("v20",0,0,0x2c,1,1,0,0x7fff,0,0x240,0x2d0,0,0,0,0,0x0c02,1,0,0x1000,0,0x1000);
die "PrintControl size\n" unless length($pc)==71;
$chunks{$pc_h}=[$pc,3]; $chunks{$pc_hints}=[pack('v8',0x4005,4,0x4047,4,0x4006,4,0x4007,4),2];
$chunks{0x40}=["\0",2];
$chunks{$hm}=[base(80,sibling=>[$primary_h|1,SAME_BLOCK],child=>[$ht,SAME_BLOCK],moniker=>$hm_m,hints=>$hm_hints).pack('C',2),3];
$chunks{$hm_m}=[moniker('Hilfe',0),2]; $chunks{$hm_hints}=[pack('vv',0x4005,4),2];
$chunks{$ht}=[base(68,sibling=>[$hm|1,SAME_BLOCK],moniker=>$ht_m,states=>0xc004).pack('vvv',0,0x1000,0x0c04),7];
$chunks{$ht_m}=[moniker('Hilfe zum PDF-Betrachter',0),2];
# view: grows with the window (max 10000 like the GeoManager folder views,
# hints 0x4015/0x4014 expand to the parent)
my $vh=0x4c;
{ my $v=$chunks{$view_h}[0]; set16(\$v,0x36,10000); set16(\$v,0x38,10000); set16(\$v,0x18,$vh); $chunks{$view_h}[0]=$v; }
$chunks{$vh}=[pack('v4',0x4015,4,0x4014,4),2];
# menu "Bearbeiten": only the dialog "Text der Seite" (resource 11); Motif
# makes a menu item that opens it, like Disk/Platte -> Diskette kopieren of
# GeoManager
$chunks{$em}=[base(80,sibling=>[$hm,SAME_BLOCK],child=>[0x24,0x100b],moniker=>$em_m,hints=>$em_hints).pack('C',2),3];
$chunks{$em_m}=[moniker('Bearbeiten',0),2]; $chunks{$em_hints}=[pack('vv',0x4005,4),2];
my $count=($em_hints+2-0x20)/2; $count++ if $count%2;
$res[3]{data}=object_block(3,\%chunks,$count);
my @menu_items=(@items,1,1);

# ---- application block with icon moniker list (resource 4) ----
my $app=pack('v14',0x3000,71,0,8,0,0,0,0,0x24,0x1003,0x2a,0,0,0x4000);
$app.="\0" x (198-length($app));
set16(\$app,28,0x28); substr($app,30,4)=$token; substr($app,197,1)=pack('C',0x80);
my @types=([5,0x20c6],[6,0x20c1],[7,0x10c6],[8,0x10c1],[9,0x20e1],[10,0x10e1]);
my $list=pack('vvv',0x1081,0x26,0x4000); $list.=pack('vvv',$_->[1],0x22,0x1000|$_->[0]) for @types;
$res[4]{data}=object_block(4,{0x24=>[$app,3],0x26=>[text_moniker($title),2],0x28=>[pack('v4',0x24,0x1003,0,0),2],0x2a=>[$list,2]},6);
for my $t (@types) {
  my ($n)=@$t; my $b=object_block($n,{0x22=>[slurp("$dir/mon_$n.bin"),2]},2);
  substr($b,4,2)=pack('v',0xc000); push @res,{data=>$b,flags=>0x1c7a,relocations=>[[0x23,0,0]]};
}
# ---- resource 11: dialog "Text der Seite" ----
# GenInteraction attributes 0x62 = dialog (as the GeoManager dialogs),
# GenText laid out like the text of the Notizblock, reply bar (hint 0x4042)
# with "Alles kopieren" (to us, 0x0c05), "Kopieren" (MSG_COPY 0x011f straight
# to the GenText) and "Schliessen" (attribute 0x80: ends the dialog)
{
  my %d;
  my ($dl,$dl_m,$tx,$tx_t,$tx_h,$rb,$rb_h,$t1,$t1_m,$t1_h,$t2,$t2_m,$t3,$t3_m,$dl_h)=map {0x24+2*$_} 0..14;   # 0x22 stays a data chunk (object_block)
  $d{$dl}=[base(80,sibling=>[$em|1,0x1003],child=>[$tx,SAME_BLOCK],moniker=>$dl_m,hints=>$dl_h).pack('C',0x62),3];
  $d{$dl_m}=[moniker('Text der Seite',0),2];
  $d{$dl_h}=[pack('vv',0x402a,4),2];
  my $text=base(86,sibling=>[$rb,SAME_BLOCK],hints=>$tx_h)
     .pack('v',$tx_t).pack('H*','4000002000000000'.'1a00'.'a861'.'0000'.'4680'.'1000'.'2c80'.'00000000'.'10'.'0000');
  die "GenText size\n" unless length($text)==57;
  $d{$tx}=[$text,7]; $d{$tx_t}=["\0",2];
  $d{$tx_h}=[pack('v8',0x4218,4,0x4012,4,0x4013,4,0x4200,8).pack('v2',0x600f,0),2];
  $d{$rb}=[base(80,sibling=>[$dl|1,SAME_BLOCK],child=>[$t1,SAME_BLOCK],hints=>$rb_h).pack('C',2),3];
  $d{$rb_h}=[pack('vv',0x4042,4),2];
  $d{$t1}=[base(68,sibling=>[$t2,SAME_BLOCK],moniker=>$t1_m,hints=>$t1_h).pack('vvv',0,0x1000,0x0c05),7];
  $d{$t1_m}=[moniker('Alles kopieren',0),2]; $d{$t1_h}=[pack('vv',0x4003,4),2];
  $d{$t2}=[base(68,sibling=>[$t3,SAME_BLOCK],moniker=>$t2_m).pack('vvv',$tx,SAME_BLOCK,0x011f),7];
  $d{$t2_m}=[moniker('Kopieren',0),2];
  $d{$t3}=[base(68,sibling=>[$rb|1,SAME_BLOCK],moniker=>$t3_m,states=>0xc080).pack('vvv',0,0,0),7];
  $d{$t3_m}=[moniker("Schlie\xa7en",0xfe),2];
  my $c=($dl_h+2-0x20)/2; $c++ if $c%2;
  push @res,{data=>object_block(11,\%d,$c),flags=>$res[3]{flags},relocations=>[[0x23,0,0]]};
}
my $new=build_geode(name=>$g->{name},extension=>$g->{extension},long_name=>$title,token=>$token,
  attributes=>$g->{attributes},file_type=>1,release=>$g->{release},protocol=>$g->{protocol},
  kernel_protocol=>$g->{kernel_protocol},udata_size=>$g->{udata_size},resources=>\@res,
  imports=>[(map {{name=>$_->{name},attributes=>$_->{attributes},protocol=>$_->{protocol}}} @{$g->{imports}}),
            {name=>'spool',attributes=>16384,protocol=>[613,0]}],
  process_class=>[1,0x40],application_object=>[4,0x24]);
open my $f,'>:raw',$out or die; print $f $new->{raw}; close $f;
printf "%s: %d bytes, %d resources, menu items %d\n",$out,length($new->{raw}),scalar @res,scalar @menu_items;
