make
rm -rf fs_result

echo "\n\n\n## extended filesystem 'functionality' test ##"
echo "- Results are saved in fs_result directory.\n\n\n"

mkdir fs_result
cd fs_result

array_succ='dir-mkdir dir-rmdir dir-vine grow-create grow-seq-sm grow-sparse'
array_fail='dir-mk-tree dir-rm-tree grow-seq-lg grow-two-files grow-tell grow-file-size grow-dir-lg grow-root-sm grow-root-lg syn-rw'

for value in $array_fail
do
  pintos -v -k -T 60 --qemu --filesys-size=8 -p ../build/tests/filesys/extended/$value -a $value -- -q  -f run $value > result_$value
done
