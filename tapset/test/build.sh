function usage {
   echo -ne " \033[1mUsage:\033[0m build [compile|clean|run]\n"
   echo -ne " \033[1mDo not remove this script from this dir!\033[0m\n"
   exit
}
function clean {
   rm -rf bin
}
function compile {
   if [ ! -d bin ]; then
      mkdir bin
   else
      rm -rf bin/*
   fi
   for file in `ls *.c`
   do
      execn=e_`echo $file|cut -d"." -f1`
      # some depend on external realtime lib...
      if [[ `echo $file|grep clock|wc -l` -gt 0
         || `echo $file|grep timer|wc -l` -gt 0 ]]
      then
         gcc -lrt $file -o bin/$execn
      else
         gcc $file -o bin/$execn
      fi
   done
   if [ `ls *.c|wc -l` -eq `ls bin|wc -l` ]; then
      echo "Success: compiled `ls bin|wc -l` files."
   else
      echo "Some files failed to compile! Try again."
   fi
}
function run {
   if [ ! -d bin -o `ls bin|wc -l` -le 0 ]; then
      echo "No compiled C files! First build compile!"
      exit
   else
      for execn in `ls bin`
      do
         echo "$execn _______________________________"
         ./bin/$execn
      done
   fi
}

if [ $# -ne 1 ];          then
   usage
elif [ $1 == "compile" ]; then
   compile
elif [ $1 == "clean" ];   then
   clean
elif [ $1 == "run" ];     then
   run
else
   usage
fi
