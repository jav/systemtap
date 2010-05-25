# Measure probe performance.  Currently measures: 
# static user uprobes, static user kprobes, dynamic user uprobes.

# example use:
# ./bench.sh -stapdir /foo/stap/install/ -gccdir /foo/gcc-4.4.3-10/install/

function setup_test() {
$STAP/bin/dtrace -G -s bench_.d
$STAP/bin/dtrace --types -h -s bench_.d
if [ "$3"x = "no-semx" ] ; then
   sed -i -e '/STAP_HAS_SEMAPHORES/d' bench_.h
fi
# Run bench without stap
$GCC/bin/gcc -D$1 -DLOOP=10 bench_.o bench.c -o bench-$2$3.x -I. -g
./bench-$2$3.x > /dev/null
taskset 1 /usr/bin/time ./bench-$2$3.x >| /tmp/$$-2 2>&1
# Parse /usr/bin/time output to get elapsed time
cat /tmp/$$-2 | awk --non-decimal-data '
function seconds(s) {
    if (index(s,":"))
	m=substr(s,0,index(s,":"))*60
    else m=0
    return m + substr(s,index(s,":")+1) 
}
/elapsed/ {
  print seconds($3)
}' >/tmp/$$-1
printf "without stap elapsed time is %s\n" $(cat /tmp/$$-1)
}

function stap_test() {
$STAP/bin/stap -DSTP_NO_OVERLOAD=1 -t -g -p4 -m stapbenchmod -c ./bench-$2$3.x bench.stp ./bench-$2$3.x $1 >/dev/null 2>&1
taskset 1 /usr/bin/time $STAP/bin/staprun stapbenchmod.ko -c ./bench-$2$3.x >| /tmp/$$-2 2>&1
# Parse /usr/bin/time, bench.x, bench.stp output to get statistics
cat /tmp/$$-2 | awk --non-decimal-data -v nostapet=$(cat /tmp/$$-1) '
function seconds(s) {
    if (index(s,":"))
	m=substr(s,0,index(s,":"))*60
    else m=0
    return m + substr(s,index(s,":")+1) 
}

# probe count and average probe setup cycles from bench.stp
/@count/ {
  n += 1
  count += (substr($2,8));
  avg += (substr($6,6))
}

# elapsed time from /usr/bin/time
/elapsed/ {
  elapsed=(seconds($3))
  print "with stap elapsed time is " elapsed
}

# average probe cycles from bench.x
/_cycles/ {
  cycles_n += 1
  cycles += $2
}

END {
  printf "count of probe hits is %s\naverage setup per probe measured via the rdtsc instruction is %d\n",count,(avg / n)
  printf "average cycles/probe %s\n",(cycles / cycles_n)
  printf "seconds/probe (%s/%s) is %f\n",elapsed-nostapet,count,(elapsed-nostapet)/count
}'

}

# Main

while test ! -z "$1" ; do
    if [ "$1" = "-gccdir" ] ; then GCC=$2 ; shift
    elif [ "$1" = "-stapdir" ] ; then STAP=$2 ; shift
    elif [ "$1" = "-k" ] ; then KEEP=1 ;
    elif [ "$1" = "-h" -o "$1" = "-help" -o "$1" = "?" ] ; then
        echo 'Usage $0 [-k] [-stapdir /stap/top/dir] [-gccdir /gcc/top/dir] [-help]'
        exit
    else echo Unrecognized arg "$1" 
        exit
    fi
   shift
done

if [ ! -z "$GCC" ] ; then
 if [ ! -x "$GCC/bin/gcc" ] ; then
    echo $GCC/bin/stap does not exist
    exit
 fi
else
 GCC=/usr/
 echo Using /usr/bin/gcc
fi

if [ ! -z "$STAP" ] ; then
 if [ ! -x "$STAP/bin/stap" ] ; then
    echo $STAP/bin/stap does not exist
    exit
 fi
else
 STAP=/usr/
 echo Using /usr/bin/stap
fi

echo -e "\n##### NO SDT #####\n"
setup_test  NO_STAP_SDT nosdt
stap_test NO_STAP_SDT nosdt

echo -e "\n##### KPROBE #####\n"
setup_test  EXPERIMENTAL_KPROBE_SDT kprobe
stap_test EXPERIMENTAL_KPROBE_SDT kprobe

echo -e "\n##### KPROBE NO SEM #####\n"
setup_test  EXPERIMENTAL_KPROBE_SDT kprobe no-sem
stap_test EXPERIMENTAL_KPROBE_SDT kprobe no-sem

echo -e "\n##### UPROBE #####\n"
setup_test UPROBE_SDT uprobe
stap_test UPROBE_SDT uprobe

echo -e "\n##### UPROBE NO SEM #####\n"
setup_test UPROBE_SDT uprobe no-sem
stap_test UPROBE_SDT uprobe no-sem

echo -e "\n##### UPROBE V2 #####\n"
setup_test STAP_SDT_V2 uprobe
stap_test STAP_SDT_V2 uprobe

echo -e "\n##### UPROBE V2 NO SEM #####\n"
setup_test STAP_SDT_V2 uprobe no-sem
stap_test STAP_SDT_V2 uprobe no-sem

if [ -z "$KEEP" ] ; then
   rm /tmp/$$-1 /tmp/$$-2
else
   echo -e "\nsaved temp files " /tmp/$$-1 /tmp/$$-2
fi

