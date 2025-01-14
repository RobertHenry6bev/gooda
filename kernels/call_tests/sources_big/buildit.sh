#! /bin/bash
set -x
gcc -o generator generator.c
./generator $1 $2 $3
gcc -O0 -g -c FOO_[0-9]*.c
gcc -O0 -g -c FOO_main.c
gcc -O0 -g FOO_main.o FOO_0*.o -o FOO_static
printf '%s\n' "finished static"
gcc -O0 -g -fPIC -c FOO_[0-9]*.c
depth=`ls FOO_000_000_[0-9]*.o | wc -l`


for (( i = depth-1 ; i >= 0; i-- )) ; do
	j=$(( i + 1 ))
	printf -v lev3 "%03d" $i
	tail="-L. -lFOO-level$j"
	[ ! -f libFOO-level$j.so ] && tail=""
	gcc --shared -o libFOO-level$i.so FOO_[0-9]*_[0-9]*_$lev3.o $tail
done
printf '%s\n' "finished shared objects"
gcc -o FOO_dynamic -O0 -g FOO_main.o -L. -lFOO-level0
