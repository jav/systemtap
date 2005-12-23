# usage
if [ $# -ne 2 ]; then
   echo -ne " \033[1mUsage:\033[0m ctostp src dest\n"
   echo -ne "   \033[1msrc:\033[0m\n"
   echo -ne "\tthe  source  directory containing  the\n"
   echo -ne "\tC files used to generate the stp files\n" 
   echo -ne "   \033[1mdest:\033[0m\n"
   echo -ne "\tthe   directory  that  will  store the\n"
   echo -ne "\tresulting  stp files\n"
   exit
fi
# strip trailing /
src=` echo $1|sed -e 's/\/$//'`
dest=`echo $2|sed -e 's/\/$//'`
# do some sanity checks
if [ `ls $src/*.c|wc -l` -le 0 ]; then
   echo "ERROR: No C files found in $src"
   exit
fi
if [ ! -d $dest ]; then
   echo "ERROR: $dest does not exist!"
   exit
else
   #clear it out
   rm -f $dest/*
fi
# ctostp
for file in `ls $src/*.c`
do
   while read line
   do
      if [ `echo $line|grep ___________|wc -l` -eq 1 ]
      then
         fn=`basename $file|cut -d"." -f1`
         echo "probe kernel.syscall.$fn {"     >> $dest/$fn.stp
         echo "   if(execname()==\"e_$fn\") {" >> $dest/$fn.stp
      fi
      # ugly? yes. effective? yes.
      if [[ `echo $line|grep dmsg|wc -l` -eq 1
      && `echo $line|grep char|wc -l` -eq 0 ]]; then
         func=`echo $line|cut -d"\"" -f2`
         var=` echo $line|cut -d"\"" -f4`
         if [ `echo $var|grep void|wc -l` -eq 1 ] 
         then
            echo "      log(\"$func: $var = \".string(0))"    >> $dest/$fn.stp
         else 
            echo "      log(\"$func: $var = \".string($var))" >> $dest/$fn.stp
         fi
      fi
   done < $file
   #close it up
   echo "   }"   >> $dest/$fn.stp
   echo -e "}\n" >> $dest/$fn.stp
done
