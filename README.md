# PSORT

Parallelly sorting file

# Examples

```sh
psort -k 1 src.txt > dst.txt
```
Sort by the first column

```sh
psort -k $'1[2-]' -M 4 reads.fq >reads.srt.fq
```
Sort fastq file by sequence name. Here, 1[2-] means the 2th char to end of the 1th column

```sh
psort -k $'r1[2-]' -S '>' reads_1.fa reads_2.fa.gz >reads.srt.fa
```
Sort fasta file by sequence name in DSC order

```sh
psort -k $'3{I II III IV}' src.txt >dst.txt
```
Sort 3th column by enum order I, II, III, IV. Undefined string will be MAX in value

```sh
$> psort -k $'1n[3-],n2' -s '/ \t' -S '>' read1.fa read2.fa | grep '^>'
>r1/1
>r1/2
>r2/1
>r2/2
...
```

