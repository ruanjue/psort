/*
 *
 * Copyright (c) 2011, Jue Ruan <ruanjue@gmail.com>
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "list.h"
#include "string.h"
#include "hashset.h"
#include "thread.h"
#include "filereader.h"
#include "bitvec.h"
#include "strnumcmp-in.h"
#include <regex.h>

//#define TYPE_STR_BIT	0
#define TYPE_NUM_BIT	0
#define TYPE_ENUM_BIT	1
#define TYPE_CASE_BIT	2
#define TYPE_REV_BIT	3

typedef struct {
	u8i off:48, len:16;
} col_t;
define_list(colv, col_t);

typedef struct {
	int field, idx;
	int beg, end;
	u1i flag;
	cuhash *hash;
} pkey_t;
define_list(pkeyv, pkey_t);

typedef struct {
	u2i off, len;
} col_brk_t;
define_list(colbrkv, col_brk_t);

typedef struct {
	long double nval;
	u4i eval;
	u8i beg, end;
} pkey_val_t;
define_list(pkeyvalv, pkey_val_t);

typedef struct {
	u1i      delimiters[256]; // 1: col, 2 : line, 3, record_header, or 0
	u4i      nline;
	u4i      rec_head;
	pkeyv    *pkeys;
	cplist   *estr;
	u1v      *text;
	BitVec   *bits;
	colv     *cols;
	colbrkv  **brks;
	u8v      *srts;
} PSRT;

PSRT* init_psrt(){
	PSRT *g;
	g = calloc(sizeof(PSRT), 1);
	g->nline = 1;
	g->rec_head = 0;
	g->pkeys = init_pkeyv(4);
	g->estr = init_cplist(32);
	g->text = init_u1v(1024);
	g->bits = init_bitvec(1024);
	g->cols = init_colv(32);
	g->brks = NULL;
	g->srts = init_u8v(32);
	return g;
}

void free_psrt(PSRT *g){
	pkey_t *pkey;
	u4i i;
	for(i=0;i<g->pkeys->size;i++){
		pkey = ref_pkeyv(g->pkeys, i);
		free_cuhash(pkey->hash);
		if(g->brks) free_colbrkv(g->brks[i]);
	}
	free_pkeyv(g->pkeys);
	for(i=0;i<g->estr->size;i++){
		free(get_cplist(g->estr, i));
	}
	free_cplist(g->estr);
	free_u1v(g->text);
	free_bitvec(g->bits);
	free_colv(g->cols);
	if(g->brks) free(g->brks);
	free_u8v(g->srts);
	free(g);
}

static int ch_parse_idx  = 0;
static int ch_parse_flag = 0;

void beg_parse_char(){
	ch_parse_idx  = 0;
	ch_parse_flag = 0;
}

int parse_char(char *str, int len){
	char c;
	while(ch_parse_idx < len){
		c = str[ch_parse_idx ++];
		if(ch_parse_flag){
			ch_parse_flag = 0;
			switch(c){
				case 't': return '\t';
				case 'n': return '\n';
				case '\\': return '\\';
				case 'b': return '\b';
				case 'r': return '\r';
				default:
				fprintf(stderr, "Parse Char Error: '\\%c' at %dth of %s\n", c, ch_parse_idx, str);
			}
		} else {
			if(c == '\\'){
				ch_parse_flag = 1;
			} else {
				return c;
			}
		}
	}
	return -1;
}

int parse_pkey_types(PSRT *g, char *optarg){
	pkey_t *pkey;
	char *ptr, ch;
	u4i i;
	int state, len, inc;
	ptr = optarg;
	state = 0;
	len = 0;
	inc = 0;
	{
		pkey = next_ref_pkeyv(g->pkeys);
		pkey->flag   = 0;
		pkey->field  = 0;
		pkey->beg    = -1;
		pkey->end    = -1;
		pkey->hash   = init_cuhash(1);
	}
	while((ch = *ptr)){
		if(ch == ','){
			{
				pkey = next_ref_pkeyv(g->pkeys);
				pkey->flag   = 0;
				pkey->field  = 0;
				pkey->beg    = -1;
				pkey->end    = -1;
				pkey->hash   = init_cuhash(1);
			}
			state = 0;
			len = 0;
		} else {
			switch(state){
				case 0:
					if(ch == 'f'){
						pkey->flag |= 1 << TYPE_CASE_BIT;
					} else if(ch == 'r'){
						pkey->flag |= 1 << TYPE_REV_BIT;
					} else if(ch == 'n'){
						pkey->flag |= 1 << TYPE_NUM_BIT;
					} else if(ch == 'e'){
						pkey->flag |= 1 << TYPE_ENUM_BIT;
					} else if(ch >= '0' && ch <= '9'){
						pkey->field = pkey->field * 10 + (ch - '0');
					} else if(ch == '['){
						state = 1;
					} else if(ch == '{'){
						state = 3;
						pkey->flag |= 1 << TYPE_ENUM_BIT;
						len = 0;
					} else {
						fprintf(stderr, "Parse Error: %s, at %dth\n", optarg, (int)(ptr + 1 - optarg)); return 1;
					}
					break;
				case 1:
					if(pkey->beg == -1) pkey->beg = 0;
					if(ch >= '0' && ch <= '9'){
						pkey->beg = pkey->beg * 10 + (ch - '0');
					} else if(ch == '-'){
						state = 2;
					} else if(ch == ']'){
						pkey->end = pkey->beg;
						state = 0;
					} else {
						fprintf(stderr, "Parse Error: %s, at %dth\n", optarg, (int)(ptr + 1 - optarg)); return 1;
					}
					break;
				case 2:
					if(pkey->end == -1) pkey->end = 0;
					if(ch >= '0' && ch <= '9'){
						pkey->end = pkey->end * 10 + (ch - '0');
					} else if(ch == ']'){
						state = 0;
					} else {
						fprintf(stderr, "Parse Error: %s, at %dth\n", optarg, (int)(ptr + 1 - optarg)); return 1;
					}
					break;
				case 3:
					if(ch == ' ' || ch == '}'){
						if(len){
							char *str;
							str = malloc(len + 1);
							strncpy(str, ptr - len, len);
							str[len] = '\0';
							push_cplist(g->estr, str);
							put_cuhash(pkey->hash, (cuhash_t){str, pkey->hash->count});
						}
						len = 0;
						if(ch == '}') state = 0;
					} else {
						len ++;
					}
					break;
				default:
					fprintf(stderr, "Parse Error: %s, at %dth\n", optarg, (int)(ptr + 1 - optarg)); return 1;
			}
		}
		ptr ++;
	}
	g->brks = malloc(sizeof(colbrkv*) * g->pkeys->size);
	for(i=0;i<g->pkeys->size;i++){
		pkey = ref_pkeyv(g->pkeys, i);
		if(pkey->field) pkey->field --;
		if(pkey->beg <= 0) pkey->beg = 0;
		else pkey->beg --;
		g->brks[i] = init_colbrkv(1024);
	}
	return 0;
}

thread_beg_def(mp);
PSRT *g;
u8i end;
int eof;
thread_end_def(mp);

thread_beg_func(mp);
PSRT *g;
u4v *colmap;
pkey_t *key;
col_brk_t *brk;
u8i ptr, beg, lst, end;
u4i i, nl, c, d, D, nc;
char tc;
g = mp->g;
ptr = lst = beg = 0;
nl = nc = 0;
d = 0;
colmap = adv_init_u4v(32, 1, 0);
for(i=0;i<g->pkeys->size;i++){
	key = ref_pkeyv(g->pkeys, i);
	if(key->field >= (int)colmap->size){
		encap_u4v(colmap, key->field + 1 - colmap->size);
		colmap->size = key->field + 1;
	}
	colmap->buffer[key->field] = i + 1;
}
thread_beg_loop(mp);
end = mp->end;
for(;ptr<end;ptr++){
	D = d;
	d = g->delimiters[g->text->buffer[ptr]];
	if(d == 0) continue;
	if(d == 3){
		if(D != 2) continue;
	}
	if(nc < colmap->size && (c = colmap->buffer[nc])){
		c --;
		key = ref_pkeyv(g->pkeys, c);
		brk = next_ref_colbrkv(g->brks[c]);
		brk->off = lst - beg;
		brk->len = ptr - lst;
		if(brk->len <= key->beg){
			brk->len = 0;
		} else {
			if(key->end > key->beg && brk->len > key->end){
				brk->len = key->end;
			}
			brk->off += key->beg;
			brk->len -= key->beg;
		}
		if(key->flag & (1 << TYPE_ENUM_BIT)){
			u4i eval;
			tc = g->text->buffer[beg + brk->off + brk->len];
			g->text->buffer[beg + brk->off + brk->len] = 0;
			eval = (u4i)getval_cuhash(key->hash, (char*)g->text->buffer + beg + brk->off);
			g->text->buffer[beg + brk->off + brk->len] = tc;
			brk->len = (u2i)eval;
		}
	}
	nc ++;
#ifdef COL_BITS
	one_bitvec(g->bits, ptr);
#endif
	lst = ptr + 1;
	if(d == 2){
		if(g->rec_head){
			continue;
		} else {
			nl ++;
			if(nl < g->nline) continue;
		}
	} else if(d == 1){
		continue;
	}
	{
		while(nc < colmap->size){
			if((c = colmap->buffer[nc++])){ push_colbrkv(g->brks[c - 1], (col_brk_t){0, 0}); }
		}
		push_colv(g->cols, (col_t){beg, ptr + 1 - beg});
		push_u8v(g->srts, g->srts->size);
		beg = ptr + 1;
		nl = 0;
		nc = 0;
	}
}
if(mp->eof && ptr > beg){
	while(nc < colmap->size){
		if((c = colmap->buffer[nc++])){ push_colbrkv(g->brks[c - 1], (col_brk_t){0, 0}); }
	}
	push_colv(g->cols, (col_t){beg, ptr - beg});
	push_u8v(g->srts, g->srts->size);
	beg = ptr;
	nl = 0;
	nc = 0;
#ifdef COL_BITS
	one_bitvec(g->bits, ptr);
#endif
}
thread_end_loop(mp);
free_u4v(colmap);
thread_end_func(mp);

void parse_data(PSRT *g, int nfile, char **filenames){
	FILE *file;
	char *filename;
	int fi, filetype;
	u4i batch_size;
	thread_preprocess(mp);
	batch_size = 64 * 1024;
	thread_beg_init(mp, 1);
	mp->g = g;
	mp->end = 0;
	mp->eof = 0;
	thread_end_init(mp);
	thread_beg_operate(mp, 0);
	for(fi=0;fi<nfile;fi++){
		filename = filenames[fi];
		if(filename == NULL || strcmp(filename, "-") == 0){
			file = stdin;
			filetype = 0;
		} else {
			if(!file_exists(filename)){
				fprintf(stderr, " -- Cannot find file %s in %s -- %s:%d --\n", filename, __FUNCTION__, __FILE__, __LINE__); fflush(stderr);
				exit(1);
			}
			if(strlen(filename) > 3 && strcmp(filename + strlen(filename) - 3, ".gz") == 0){
				char *cmd;
				cmd = alloca(strlen(filename) + 10);
				sprintf(cmd, "gzip -dc %s", filename);
				file = popen(cmd, "r");
				filetype = 1;
			} else {
				file = open_file_for_read(filename, NULL);
				filetype = 2;
			}
		}
		while(1){
			encap_u1v(g->text, batch_size);
#ifdef COL_BITS
			encap_bitvec(g->bits, batch_size);
#endif
			thread_wake(mp);
			u4i nb;
			nb = fread(g->text->buffer + g->text->size, 1, batch_size, file);
			g->text->size += nb;
#ifdef COL_BITS
			g->bits->n_bit += nb;
#endif
			thread_wait(mp);
			mp->end = g->text->size;
			if(nb == 0){
				mp->eof = 1;
				thread_wake(mp);
				break;
			}
		}
		thread_wait(mp);
		if(filetype == 1){
			pclose(file);
		} else if(filetype == 2){
			fclose(file);
		}
	}
	thread_end_operate(mp, 0);
	thread_beg_close(mp);
	thread_end_close(mp);
#ifdef COL_BITS
	one2bitvec(g->bits);
#endif
}

static u1i is_num_chars[256];

int cmp_col(PSRT *g, u8i a, u8i b){
	pkey_t *pkey;
	col_t *ka, *kb;
	col_brk_t *ca, *cb;
	u4i i, c;
	int cmp;
	char *sa, *sb, ta, tb;
	for(i=0;i<g->pkeys->size;i++){
		pkey = ref_pkeyv(g->pkeys, i);
		c = pkey->field;
		ca = ref_colbrkv(g->brks[i], a);
		cb = ref_colbrkv(g->brks[i], b);
		if(pkey->flag & (1 << TYPE_NUM_BIT)){
			ka = ref_colv(g->cols, a);
			kb = ref_colv(g->cols, b);
			sa = (char*)g->text->buffer + ka->off + ca->off;
			if(is_num_chars[(int)sa[ca->len]]){
				ta = sa[ca->len];
				sa[ca->len] = 0;
			} else {
				ta = 0;
			}
			sb = (char*)g->text->buffer + kb->off + cb->off;
			if(is_num_chars[(int)sb[cb->len]]){
				tb = sb[cb->len];
				sb[cb->len] = 0;
			} else {
				tb = 0;
			}
			cmp = numcompare(sa, sb, '.', ',');
			if(ta) sa[ca->len] = ta;
			if(tb) sb[cb->len] = tb;
			if(cmp == 0) continue;
			if(pkey->flag & (1 << TYPE_REV_BIT)){
				return - cmp;
			} else {
				return cmp;
			}
		} else if(pkey->flag & (1 << TYPE_ENUM_BIT)){
			if(ca->len == cb->len) continue;
			if(pkey->flag & (1 << TYPE_REV_BIT)){
				if(ca->len > cb->len) return -1;
				else return 1;
			} else {
				if(ca->len > ca->len) return 1;
				else return -1;
			}
		} else {
			ka = ref_colv(g->cols, a);
			kb = ref_colv(g->cols, b);
			if(ca->len > cb->len){
				cmp = __builtin_memcmp(g->text->buffer + ka->off + ca->off, g->text->buffer + kb->off + cb->off, cb->len);
				if(cmp == 0) cmp = 1;
			} else if(ca->len < cb->len){
				cmp = __builtin_memcmp(g->text->buffer + ka->off + ca->off, g->text->buffer + kb->off + cb->off, ca->len);
				if(cmp == 0) cmp = -1;
			} else {
				cmp = __builtin_memcmp(g->text->buffer + ka->off + ca->off, g->text->buffer + kb->off + cb->off, ca->len);
				if(cmp == 0) continue;
			}
			if(pkey->flag & (1 << TYPE_REV_BIT)){
				return - cmp;
			} else {
				return cmp;
			}
		}
	}
	return 0;
}

void psort_data(PSRT *g, int ncpu){
	int i;
	memset(is_num_chars, 0, 256);
	for(i='0';i<='9';i++) is_num_chars[i] = 1;
	is_num_chars[(int)'.'] = 1;
	is_num_chars[(int)','] = 1;
	is_num_chars[(int)'-'] = 1;
	if(ncpu > 1){
		psort_array(g->srts->buffer, g->srts->size, u8i, ncpu, cmp_col(g, a, b) > 0);
	} else {
		sort_array(g->srts->buffer, g->srts->size, u8i, cmp_col(g, a, b) > 0);
	}
}

void output_data(PSRT *g, int uniq, FILE *out){
	u8i i;
	col_t *col;
	for(i=0;i<g->srts->size;i++){
		col = ref_colv(g->cols, g->srts->buffer[i]);
		if(uniq == 1 && i){
			if(cmp_col(g, g->srts->buffer[i - 1], g->srts->buffer[i]) == 0) continue;
		}
		fwrite(g->text->buffer + col->off, 1, col->len, out);
	}
	fflush(out);
}

int usage(){
	printf(
	"psort, sort files by multiple fields\n"
	"Version: 1.0\n"
	"Author: Jue Ruan <ruanjue@gmail.com>\n"
	"Usage: psort [options] [file1] [file2.gz]\n"
	"Options:\n"
	" -h          display this document\n"
	" -t <int>    number of threads, 0: all cores, [0]\n"
	" -l <string> specify line brokers, +, \\n]\n"
	" -M <int>    treat <-M> line as one record, [1]\n"
	" -S <string> treat line start with a character in <-S> as new record, +, []\n"
	" -s <string> specify field separators, +, [\\t ]\n"
	" -u          output the first record for repeats\n"
	" -k <string> specify fields to be sorted, eg.\n"
	"             rn10[2-6]: reverse sort 2-6th (include 6) of column 10, treat it as number\n"
	"             n11: sort column 11, treat it as number\n"
	"             f2[6-]: sort 6th-end of column 2, treat it as string, ignore case\n"
	"             3{red green blue}: sort column 3, treat it as enum\n"
	"             3[2-]{red green blue}: sort column 3 (2th char to end), treat it as enum\n"
	"             n: numeric\n"
	"             r: reverse\n"
	"             f: ignore case\n"
	"             \\d+: field\n"
	"             [a-b]: substr\n"
	"             {a b c}: enum\n"
	" -m <int>    merge mode, 0: sort and merge into rows; 1: merge into rows (append row); 2: merge into columns (append col); [0]\n"
	"             when -m 2, "
	);
	return 1;
}

int main(int argc, char **argv){
	PSRT *g;
	String *param;
	char *fstdins[1];
	int ncpu, nl, ns, uniq;
	int c, pc;
	g = init_psrt();
	param = init_string(32);
	ncpu = 0;
	nl = ns = 0;
	uniq = 0;
	while((c = getopt(argc, argv, "hut:l:M:S:s:k:")) != -1){
		switch(c){
			case 't': ncpu = atoi(optarg); break;
			case 'l': beg_parse_char(); while((pc = parse_char(optarg, strlen(optarg))) != -1){ nl ++; g->delimiters[pc] = 2; } break;
			case 's': beg_parse_char(); while((pc = parse_char(optarg, strlen(optarg))) != -1){ ns ++; g->delimiters[pc] = 1; } break;
			case 'S': beg_parse_char(); while((pc = parse_char(optarg, strlen(optarg))) != -1){ g->rec_head ++; g->delimiters[pc] = 3; } break;
			case 'M': g->nline = atoi(optarg); break;
			case 'k': append_string(param, optarg, strlen(optarg)); break;
			case 'u': uniq = 1; break;
			default: return usage();
		}
	}
	if(nl == 0) g->delimiters[(int)'\n'] = 2;
	if(ns == 0){ g->delimiters[(int)'\t'] = 1; g->delimiters[(int)' '] = 1; }
	if(param->size == 0){
		append_string(param, "1", 1);
	}
	if(ncpu <= 0){
		u8i memt, memf;
		get_linux_sys_info(&memt, &memf, &ncpu);
		if(ncpu <= 0) ncpu = 1;
	}
	parse_pkey_types(g, param->string);
	free_string(param);
	if(optind >= argc){
		fstdins[0] = "-";
		parse_data(g, 1, fstdins);
	} else {
		parse_data(g, argc - optind, argv + optind);
	}
	psort_data(g, ncpu);
	output_data(g, uniq, stdout);
	free_psrt(g);
	return 0;
}

