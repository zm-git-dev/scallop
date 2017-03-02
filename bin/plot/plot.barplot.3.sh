#!/bin/bash

dftA="scallop.B505.B"
dftB="stringtie.1.3.1c"
dftC="transcomb.2.5"
measure="gffmul"

tmp=./plot
mkdir -p $tmp

list="GSM981256.ST1 \
	  GSM981244.ST2 \
	  GSM984609.ST3 \
	  SRR387661.TC1 \
	  SRR307911.TC2 \
	  SRR545723.SC1 \
	  SRR315323.SC2 \
	  SRR307903.SC3 \
	  SRR315334.SC4 \
	  SRR534307.SC5"

tmp=./plot
mkdir -p $tmp

echo "REAL.DATA.$measure" | awk '{print toupper($1)}' > $tmp/title
file=$tmp/barplot
rm -rf $file
for ii in `echo $list`
do
	i=`echo $ii | cut -f 1 -d "."`
	j=`echo $ii | cut -f 2 -d "."`
	a1=`cat ../$i.tophat/$dftA/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	a2=`cat ../$i.star/$dftA/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	a3=`cat ../$i.hisat/$dftA/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	b1=`cat ../$i.tophat/$dftB/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	b2=`cat ../$i.star/$dftB/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	b3=`cat ../$i.hisat/$dftB/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	c1=`cat ../$i.tophat/$dftC/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	c2=`cat ../$i.star/$dftC/"$measure".roc | head -n 1 | awk '{print $10,$13,$16}'`
	c3="0 0 0"

	if [ "$a1" == "" ]
	then
		a1="0 0 0"
	fi
	if [ "$a2" == "" ]
	then
		a2="0 0 0"
	fi
	if [ "$a3" == "" ]
	then
		a3="0 0 0"
	fi
	if [ "$b1" == "" ]
	then
		b1="0 0 0"
	fi
	if [ "$b2" == "" ]
	then
		b2="0 0 0"
	fi
	if [ "$b3" == "" ]
	then
		b3="0 0 0"
	fi
	if [ "$c1" == "" ]
	then
		c1="0 0 0"
	fi
	if [ "$c2" == "" ]
	then
		c2="0 0 0"
	fi

	echo "$j $a1 $a2 $a3 $b1 $b2 $b3 $c1 $c2 $c3" >> $file
done
R CMD BATCH barplot.9.R
mv $tmp/pre.pdf $tmp/sra.$measure.pre.pdf
mv $tmp/sen.pdf $tmp/sra.$measure.sen.pdf
mv $file $tmp/sra.$measure.sen.summary
