mkdir cfiles
cp build.sh cfiles
while read line
do
   if [ `echo $line | grep "~~~~~~~~~~~~~~" | wc -l` -gt 0 ]
   then
      file=`echo $line |cut -d" " -f1`
   else
      echo "$line" >> cfiles/$file
   fi  
done < master.c
