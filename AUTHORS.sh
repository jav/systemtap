#! /bin/sh

# Create the AUTHORS file, by searching the git history.

# Run as "AUTHORS.sh" to get complete history
# Run with "AUTHORS.sh commitish..commitish" for history between tags

# Early history did not include userid->full-name mappings, so we do
# that explicitly here.

sedcmd="$sedcmd -e s/^fche$/Frank_Ch._Eigler/"
sedcmd="$sedcmd -e s/^brolley$/Dave_Brolley/"
sedcmd="$sedcmd -e s/^kenistoj$/Jim_Keniston/"
sedcmd="$sedcmd -e s/^dsmith$/David_Smith/"
sedcmd="$sedcmd -e s/^hunt$/Martin_Hunt/"
sedcmd="$sedcmd -e s/^roland$/Roland_McGrath/"
sedcmd="$sedcmd -e s/^wcohen$/Will_Cohen/"
sedcmd="$sedcmd -e s/^graydon$/Graydon_Hoare/"
sedcmd="$sedcmd -e s/^ananth$/Ananth_N_Mavinakayanahalli/"
sedcmd="$sedcmd -e s/^mbehm$/Michael_Behm/"
sedcmd="$sedcmd -e s/^bradchen$/Brad_Chen/"
sedcmd="$sedcmd -e s/^trz$/Tom_Zanussi/"
sedcmd="$sedcmd -e s/^rustyl$/Rusty_Lynch/"
sedcmd="$sedcmd -e s/^askeshav$/Anil_Keshavamurthy/"
sedcmd="$sedcmd -e s/^cspiraki$/Charles_Spirakis/"
sedcmd="$sedcmd -e s/^prasannasp$/Prasanna_S_Panchamukhi/"
sedcmd="$sedcmd -e s/^hien$/Hien_Nguyen/"
sedcmd="$sedcmd -e s/^kevinrs$/Kevin_Stafford/"
sedcmd="$sedcmd -e s/^jistone$/Josh_Stone/"
sedcmd="$sedcmd -e s/^hiramatu$/Masami_Hiramatsu/"
sedcmd="$sedcmd -e s/^markmc$/Mark_McLoughlin/"
sedcmd="$sedcmd -e s/^eteo$/Eugene_Teo/"
sedcmd="$sedcmd -e s/^guanglei$/Li_Guanglei/"
sedcmd="$sedcmd -e s/^tpnguyen$/Thang_Nguyen/"
sedcmd="$sedcmd -e s/^maobibo$/bibo_mao/"
sedcmd="$sedcmd -e s/^dwilder$/David_Wilder/"
sedcmd="$sedcmd -e s/^mmason$/Mike_Mason/"
sedcmd="$sedcmd -e s/^srikar$/Srikar_Dronamraju/"
sedcmd="$sedcmd -e s/^srinivasa$/Srinivasa_DS/"
sedcmd="$sedcmd -e s/^wenji$/Wenji_Huang/"
sedcmd="$sedcmd -e s/^ostrichfly$/Zhaolei/"
sedcmd="$sedcmd -e s/^zhaolei$/Zhaolei/"
sedcmd="$sedcmd -e s/^shli$/Shaohua_Li/"
sedcmd="$sedcmd -e s/^prasadkr$/K.Prasad/"
sedcmd="$sedcmd -e s/^dcnomura$/Dave_Nomura/"
sedcmd="$sedcmd -e s/^ddomingo$/Don_Domingo/"
sedcmd="$sedcmd -e s/^rarora$/Rajan_Arora/"
sedcmd="$sedcmd -e s/^prerna$/Prerna_Saxena/"
sedcmd="$sedcmd -e s/^dvlasenk$/Denys_Vlasenko/"
sedcmd="$sedcmd -e s/^ebaron$/Elliott_Baron/"
sedcmd="$sedcmd -e s/^ksebasti$/Kent_Sebastian/"
sedcmd="$sedcmd -e s/^maynardj$/Maynard_Johnson/"

# tweaks
sedcmd="$sedcmd -e s/^root$//"
sedcmd="$sedcmd -e s/^dcn$/Dave_Nomura/"
sedcmd="$sedcmd -e s/^Srinivasa$/Srinivasa_DS/"


# echo $sedcmd
git log --pretty=format:"%an" ${1-} | 
sed -e 's/ /_/g' | sed $sedcmd | sed -e 's/_/ /g' | 
grep . |
sort | uniq # -c
