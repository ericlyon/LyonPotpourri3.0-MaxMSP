#!/usr/bin/perl                                                                 

# Perl script to compile LyonPotpourri 3.0
# USAGE: perl buildall.pl

$here = `pwd`; # local directory
$here =~ tr/\n//d; # strip trailing carriage return


while(<*>){
    chomp;
    if(-d){
		print "BUILDING $_\n";
		$proj = $here . "/" . $_ . "/" . $_ . ".xcodeproj";
		$builddir = $here . "/" . $_ . "/build";
		`xcodebuild -project \"$proj\" -configuration Deployment`;
		`rm -rf \"$builddir\"`;
    }
}

