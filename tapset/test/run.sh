function usage {
   echo -ne " \033[1mUsage:\033[0m run stpdir bindir\n"
   echo -ne "   \033[1mstpdir:\033[0m\n"
   echo -ne "\tthe directory containing the stp files\n"
   echo -ne "   \033[1mbindir:\033[0m\n"
   echo -ne "\tthe directory containing the c executables\n"
   exit
}
function cleanup {
   rm -f tmp*
}
function even {
   k=$1
   while [ $k -ne $2 ]; do
      if [ $# -eq 3 ]; then
         echo -ne " " >> $logfile
      else
         echo -ne " " 
      fi
      k=`expr $k + 1`
   done
}
function spit {
   echo -ne "\t\t\tFUNCTION: `echo $1 | cut -d"." -f4`\n" >> $logfile
   echo "Expected output:" >> $logfile
   cat $2 >> $logfile
   echo "Actual output:"   >> $logfile
   cat $1 >> $logfile
   echo -ne "___________________________________" >> $logfile
   echo "___________________________________" >> $logfile
}
function spiteven {
   echo -ne "\t\t\tFUNCTION: `echo $1 | cut -d"." -f4`\n" >> $logfile
   echo -ne "Expected Output:\t\t\t\tActual Output:\n" >> $logfile
   i=1
   lines=`cat $1|wc -l`
   while [ $i -le $lines ]; do
      echo -ne "`head -$i $2|tail -1`"   >> $logfile
      even `head -$i $2|tail -1|wc -m` 49 9
      echo -ne "`head -$i $1|tail -1`\n" >> $logfile
      i=`expr $i + 1`
   done
   echo -ne "___________________________________" >> $logfile
   echo "___________________________________" >> $logfile
}
trap got_trap 1 2 3 6
function got_trap {
   echo -e "\nGot signaled. Cleaning up...\n"
   if [ `ps -A|grep stpd|sed 's/^[ ^t]*//'|cut -d" " -f1 | wc -l` -gt 0 ]
   then
      kill `ps -A|grep stpd|sed 's/^[ ^t]*//'|cut -d" " -f1`
   fi
   if [ `ps -A|grep stap|sed 's/^[ ^t]*//'|cut -d" " -f1 | wc -l` -gt 0 ]
   then
      kill `ps -A|grep stap|sed 's/^[ ^t]*//'|cut -d" " -f1`
   fi
   cleanup
   exit 1
}
function waitforoutput {
   t=0
   while [ `cat $1|wc -l` -eq 0 -a $t -ne 4 ]; do
      sleep 1
      t=`expr $t + 1`
   done
   if [ $t -eq 4 ]; then
      even 1 3
      echo -en "\033[0;36mPROBEMISS\033[0m\n"
      failures=`expr $failures + 1`
      #spit $1 $2
   else
      validate $1 $2
   fi
}
function validate {
   if [ `diff $1 $2|wc -l` -gt 0 ]; then
      even 1 3
      echo -en "\033[0;31mFAIL\033[0m\n"
      failures=`expr $failures + 1`
      if [ `cat $1|wc -l` -eq `cat $2|wc -l` ]; then  
         spiteven $1 $2
      else
         spit $1 $2
      fi
   else
      even 1 3
      echo -en "\033[0;32mPASS\033[0m\n"
   fi
}
# start +++++++++++++++++++++++++++++++++++
stpdir=`echo $1|sed -e 's/\/$//'`
bindir=`echo $2|sed -e 's/\/$//'`
# sanity...
if [[ ! -d $stpdir || ! -d $bindir ]]; then
   echo "Invalid arguments. Try again."
   usage
elif [ `ls $stpdir/*.stp|wc -l` -le 0 ]; then
   echo "No .stp files found in $stpdir"
   exit
elif [ `ls $bindir/|wc -l` -le 0 ]; then
   echo "No executable files found in $bindir"
   exit
elif [ $UID -ne 0 ]; then
   echo "You must be root to do that!"
   exit
elif [ ! -d log ]; then
   mkdir log
fi

total=`ls $stpdir|wc -l`
logfile=log/`date +%m%d%y_%H%M%S`.log
clear
echo -en "Prog\t\tScript"
echo -e "\t\t\t\t Status"

# iterate through script files
ct=1
failures=0
for i in `ls $stpdir`
do
   # create some tmp files for output
   stp_tmpf="tmp.stp.$$.$i"
   c_tmpf="tmp.c.$$.$i"
   echo -ne "$ct/$total:\t"
   if [ $ct -lt 100 ]; then
      echo -ne "\t"
   fi
   echo -ne $i
   #insmod the ko
   stap -DMAXNESTING=10 $stpdir/$i > $stp_tmpf &
   # make sure module is loaded
   pid=""; ast=""
   while [[ "$pid" == "" || "$ast" == "" ]]
   do
      pid=`ps|grep stpd|sed 's/^[ ^t]*//'|cut -d" " -f1`
      ast=`ps|grep stap|sed 's/^[ ^t]*//'|cut -d" " -f1`
      sleep 1
   done
   even `echo $i|wc -m` 30
   echo -n "+++"
   # now we can safely run the user c program
   su -c "./$bindir/e_`echo $i|cut -d"." -f1` > $c_tmpf" krstaffo   
   # kill off stap
   kill $pid
   # give it some breathing room
   waitforoutput $stp_tmpf $c_tmpf
   ct=`expr $ct + 1`
   cleanup
done
echo "Total Failures: $failures/$total"
