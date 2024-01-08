find $1 -name 'CMakeLists.*.txt' | xargs grep -l add_library | xargs grep -L target_sources | grep -e '/\(no_\|\)llvm[0-9]*/'| while read x;do
  echo "$x"
  libname=`grep add_library $x | awk -F'(' '{print $2}' | awk -F'[ )]' '{print $1}'`
  echo $libname
  echo "target_sources( $libname PRIVATE dummy.cpp )" >> "$x"
  touch $(dirname "$x")/dummy.cpp
  echo
done
