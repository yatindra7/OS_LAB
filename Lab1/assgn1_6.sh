sieve="$(seq 2 1000|sort)"
for n in 2 $(seq 3 2 1000)
do
  sieve="$(comm -23 <(echo "$sieve") <(seq $(($n * $n)) $n 1000|sort))"
done
while read line; do
  echo "$sieve" | sort -n | awk -v inputlimit="$line" '$1 <= inputlimit { if(inputlimit%$1==0) print }'   | sed ':a;N;$!ba;s/\n/\ /g'
done < "$1" > "output.txt"