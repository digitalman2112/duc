
#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>


#include "cmd.h"
#include "duc.h"
#include "db.h"


struct dup_options
{
	long minbytes;
	int  matchbyS;
	int  matchbyN;
	int  matchbyES;
	int  folderscan;
	int  hashcheck;
	int  caseinsensitive;
};

struct dup_totals
{
	//sum of all
	off_t total_size;
	size_t total_num;

	//name + size
	off_t matchNS_size;
	size_t matchNS_num;

	//size only
	off_t matchS_size;
	size_t matchS_num;

	//name only
	off_t matchN_size;
	size_t matchN_num;

	//extension + size
	off_t matchES_size;
	size_t matchES_num;

	size_t total_entries;
	off_t total_entries_size;
	size_t total_compared;
	off_t total_compared_size;
	size_t total_notscanned_size;
	off_t total_notscanned_size_size;
	size_t total_notscanned_dir;
	off_t total_notscanned_dir_size;
	size_t total_notscanned_fil;
	off_t total_notscanned_fil_size;

};

typedef struct {
	char *name;                 /* File name */
	off_t size;                 /* File size */
	duc_dirent_mode mode;       /* File mode */
	dev_t dev;                  /* ID of device containing file */
	ino_t ino;                  /* inode number */
	char *path;		    /* full path */
} duc_dirent2;

static long entry_num;
static struct dup_options options;
static struct dup_totals totals;
duc_dirent2 *entlist = NULL;

static int get_hash (char* path, char* hash_out) {

	printf("     Calculating hash for %s", path);

	FILE *fp;
	int status;
	int hash_len = 32;

	char cmd[PATH_MAX];
	strcpy (cmd , "md5sum ");
	strcat (cmd , "\"");
	strcat (cmd , path);
	strcat (cmd , "\"");
	strcat (cmd , " 2>&1"); //suppress stderr 

	fp=popen(cmd, "r");
	if (fp == NULL) 
		{
		printf ("\n     ERROR: Hash file handle error!\n");
		return 0;
		}

	int i;
	for (i=0; i<hash_len; i++) {
		char c = fgetc (fp);

		if (!isalnum (c)) {
			char err[PATH_MAX];
			fgets (err, sizeof err, fp);
			printf("\n     ERROR: Invalid response from hash function at byte %d = %c ;%s\n", i,c,err);
			pclose(fp);
			return 0;
		}

		hash_out[i] = c;
	}
	hash_out[hash_len] = '\0';

	printf(": %s\n", hash_out);

	status = pclose(fp);
	if (status == -1) {
		printf ("\n     ERROR: Status from hash call\n");
		return 0;
		}
	else {
		return 1;
	}

	return 0;

}
static int hash_match(char *path1, char* path2)  {

	char *hash1 = malloc(sizeof (char[PATH_MAX]));
	char *hash2 = malloc(sizeof (char[PATH_MAX]));

	int ret1 = get_hash(path1, hash1);
	if (!ret1) return -1;

	int ret2 = get_hash(path2, hash2);
	if (!ret2) return -1;

	if (strcmp(hash1, hash2) == 0) {
		 return 1;
	}
	return 0;
}


static const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

static int strequal (const char *item1, const char *item2, int insensitive) {
	if (insensitive) return !strcasecmp(item1, item2);
	else return !strcmp(item1, item2);
}

static void find_dups(duc *duc, duc_dir *dir, int depth)
{
	struct duc_dirent *e;

	while( (e = duc_dir_read(dir)) != NULL) {
		
		
		totals.total_entries++;

		if(e->mode == DUC_MODE_DIR) {
			totals.total_entries_size+=e->size;
			duc_dir *dir_child = duc_dir_openent(dir, e);
			if(dir_child) {
				find_dups(duc, dir_child, depth + 1);
			}
		}
		if ((e->mode == DUC_MODE_DIR) && (!options.folderscan)) {
			totals.total_notscanned_dir++;
			totals.total_notscanned_dir_size+=e->size;
			continue;}
		if ((e->mode == DUC_MODE_REG) && (options.folderscan)) {
			totals.total_notscanned_fil++;
			totals.total_notscanned_fil_size+=e->size;
			break;}
		if  (e->size <= options.minbytes) {
			totals.total_notscanned_size++;
			totals.total_notscanned_size_size+=e->size;
			break;
		}
		
		totals.total_compared++;
		totals.total_compared_size+=e->size;

		char fullpath[PATH_MAX];
		strcpy (fullpath , duc_dir_get_path(dir));

		//increase size of array
		entlist = (duc_dirent2*) realloc(entlist, (entry_num + 1) * sizeof (duc_dirent2));

		//copy data from ent to ent2 to add path
		duc_dirent2 *cpy = malloc(sizeof(duc_dirent2));

		cpy->name = strdup(e->name);
		cpy->size = e->size;
		cpy->mode = e->mode;
		cpy->dev  = e->dev;
		cpy->ino  = e->ino;
		cpy->path = strdup(fullpath);

		entlist[entry_num] = *cpy;

		int i;
		int priormatch = 0;
		//look for matches
		for (i=0;i< entry_num;i++){

			int matchtype = 0;
			duc_dirent2 cmp;
			cmp = entlist[i];

			if ((cmp.size == e->size) && strequal(cmp.name,e->name, options.caseinsensitive)) {
				matchtype = 1;
				
				if (e->mode == DUC_MODE_DIR) {
					printf("MATCHDIR(NAME+SIZE): ");
				} else {
					printf("MATCHFIL(NAME+SIZE): ");
				}
							
				if (!priormatch) { 
					totals.total_num++;
               		                totals.total_size+=e->size;
					totals.matchNS_num++;
					totals.matchNS_size+=e->size;
                        	 }

			}

			else if ((options.matchbyES) && (e->mode != DUC_MODE_DIR) && 
					(strequal(get_filename_ext(cmp.name),get_filename_ext(e->name), options.caseinsensitive)) && (cmp.size == e->size)) {
                                 matchtype = 2;

				if (e->mode == DUC_MODE_DIR) {
                                	printf("MATCHDIR(EXT+SIZE):  ");
                                } else { 
					printf("MATCHFIL(EXT+SIZE):  ");
                                }

                                if (!priormatch) {
                                	totals.total_num++;
                                        totals.total_size+=e->size;
                                        totals.matchES_num++;
                                        totals.matchES_size+=e->size;
                                }
                        }

			else if ((options.matchbyN) && (strequal(cmp.name,e->name, options.caseinsensitive))) {
                        	matchtype = 3;

				if (e->mode == DUC_MODE_DIR) {
 	                              printf("MATCHDIR(NAME):      ");
                                } else {
                                	printf("MATCHFIL(NAME):      ");
                                }

                                if (!priormatch) {
                              		totals.total_num++;
                                        totals.total_size+=e->size;
                                        totals.matchN_num++;
                                        totals.matchN_size+=e->size;
                                }
			}

			else if ((options.matchbyS) && (cmp.size == e->size)) {
    	            		matchtype = 4;

				if (e->mode == DUC_MODE_DIR) {
                                	printf("MATCHDIR(SIZE):      ");
                                } else {
                                	printf("MATCHFIL(SIZE):      ");
                                }

                                if (!priormatch) {
                                	totals.total_num++;
                                        totals.total_size+=e->size;
                                        totals.matchS_num++;
                                        totals.matchS_size+=e->size;
                                }

			}

			if (matchtype) {
	                        printf("%s/%s (%s)  = %s/%s (%s)\n", duc_dir_get_path(dir) , e->name,
					duc_human_size(e->size), cmp.path, cmp.name, duc_human_size(cmp.size));

				if (options.hashcheck) {
					int  h = 0;
					char *path1 = malloc(sizeof (char[PATH_MAX]));
					char *path2 = malloc(sizeof (char[PATH_MAX]));
					strcpy (path1, duc_dir_get_path(dir));
					strcat (path1, "/");
					strcat (path1, e->name);

					strcpy (path2, cmp.path);
					strcat (path2, "/");
					strcat (path2, cmp.name);

					h = hash_match(path1, path2);
					free (path1);
					free (path2);

					if (h==1) {
						printf("     FILE HASH MATCH\n");
					}
					else if (h==0) {
						printf("     NO MATCH - FILE HASH DOES NOT MATCH - NO MATCH\n");
					}
					else {
						printf("     UNABLE TO CALC HASH - PLEASE RETRY\n");
					}
					
				}

				printf("\n");
				priormatch = 1;

			}
		} //end for (iterate match array)
	 (entry_num)++;
	} //end while (entries to scan)
} 


static int dup_main(int argc, char **argv)
{
	int c;
	char *path_db = NULL;
	duc_log_level loglevel = DUC_LOG_WRN;

	options.minbytes = 0;
	options.matchbyS = 0;
	options.matchbyN = 0;
	options.matchbyES = 0;
	options.folderscan = 0;
	options.caseinsensitive = 0;
	options.hashcheck = 0;

	totals.total_size = 0;
	totals.total_num = 0;
	totals.matchNS_size = 0;
	totals.matchES_size = 0;
	totals.matchS_size = 0;
	totals.matchN_size = 0;
	totals.matchNS_num = 0;
	totals.matchES_num = 0;
	totals.matchS_num = 0;
	totals.matchN_num = 0;
        totals.total_entries = 0;
        totals.total_compared = 0;
        totals.total_notscanned_size = 0;
        totals.total_notscanned_dir = 0;
        totals.total_notscanned_fil = 0;
 
	struct option longopts[] = {
		{ "database",       required_argument, NULL, 'd' },
		{ "megabytes", 	    optional_argument, NULL, 'm' },
		{ "quiet",          optional_argument, NULL, 'q' },
		{ "verbose",        optional_argument, NULL, 'v' },
		{ "size",           optional_argument, NULL, 's' },
		{ "name",           optional_argument, NULL, 'n' },
		{ "extension",      optional_argument, NULL, 'e' },
		{ "folderscan",     optional_argument, NULL, 'f' },
		{ "insensitive",    optional_argument, NULL, 'i' },
		{ "hash", 	    optional_argument, NULL, 'h' },
		{ NULL }
	};

	while( ( c = getopt_long(argc, argv, "d:m:qvsnefih", longopts, NULL)) != EOF) {

		switch(c) {
			case 'd':
				path_db = optarg;
				break;
			case 'm':
				options.minbytes = atol(optarg)*1024000;
			case 'q':
				loglevel = DUC_LOG_FTL;
				break;
			case 'v':
				if(loglevel < DUC_LOG_DMP) loglevel ++;
				break;
			case 's':
				options.matchbyS = 1;
				break;
			case 'n':
				options.matchbyN = 1;
				break;
			case 'e':
				options.matchbyES = 1;
				break;
			case 'f':
				options.folderscan = 1;
				break;
			case 'i':
				options.caseinsensitive = 1;
				break;
			case 'h':
				options.hashcheck = 1;
				break;
			default:
				return -2;
		}
	}

	argc -= optind;
	argv += optind;
	
	char *path = ".";
	if(argc > 0) path = argv[0];
	printf("Starting duplicate scan on path: %s\n\n", path);
	
	/* Open duc context */
	
	duc *duc = duc_new();
	if(duc == NULL) {
                fprintf(stderr, "Error creating duc context\n");
                return -1;
        }

	duc_set_log_level(duc, loglevel);

	int r = duc_open(duc, path_db, DUC_OPEN_RO);
	if(r != DUC_OK) {
		fprintf(stderr, "%s\n", duc_strerror(duc));
		return -1;
	}



	duc_dir *dir = duc_dir_open(duc, path);
	if(dir == NULL) {
		fprintf(stderr, "%s\n", duc_strerror(duc));
		return -1;
	}

	off_t pathsize = duc_dir_get_size(dir);

	entry_num = 0;

	struct timeval time_start;
	gettimeofday(&time_start, NULL);
	

	find_dups(duc, dir, 1);

	struct timeval time_stop;
	gettimeofday(&time_stop, NULL);


//	duc_dir *dir2 = duc_dir_open(duc, path);

	//free memory
	free (entlist);

	printf ("\n\n");
	printf ("Scan Path:     %s (%s)\n", path, duc_human_size(pathsize));
	printf ("Scanned:  %10ld index entries in %s\n", totals.total_entries, duc_human_duration(time_start, time_stop));
	printf ("Ignored:  %10ld index entries below minimum size   (%ld megabytes, use -m to set)\n", totals.total_notscanned_size, options.minbytes / 1024000);

	if (!options.folderscan) {
	printf ("Ignored:  %10ld index entries that are folders     (-f not enabled)\n", totals.total_notscanned_dir);
	} else {
	printf ("Ignored:  %10ld index entries  that are not folders (-f enabled)\n", totals.total_notscanned_fil);
	}


	printf ("Compared: %10ld index entries\n", totals.total_compared);
	printf ("\n");
	printf ("Sum of All  Matches: %10ld    Size: %6s  %.2f%%\n", totals.total_num  , duc_human_size(totals.total_size), (double)totals.total_size/pathsize*100);
	printf ("(NAME+SIZE) Matches: %10ld    Size: %6s  %.2f%%\n", totals.matchNS_num, duc_human_size(totals.matchNS_size), (double)totals.matchNS_size/pathsize*100);

	if (options.matchbyES) { printf ("(EXT+SIZE)  Matches: %10ld    Size: %6s  %.2f%%\n", totals.matchES_num, duc_human_size(totals.matchES_size), (double)totals.matchES_size/pathsize*100); }
	if (options.matchbyS)  { printf ("(SIZE)      Matches: %10ld    Size: %6s  %.2f%%\n", totals.matchS_num , duc_human_size(totals.matchS_size) , (double)totals.matchS_size/pathsize*100);  }
	if (options.matchbyN)  { printf ("(NAME)      Matches: %10ld    Size: %6s  %.2f%%\n", totals.matchN_num , duc_human_size(totals.matchN_size) , (double)totals.matchNS_size/pathsize*100);  }

	printf ("\n");
	printf ("NOTE: THIS TOOL PROVIDES A WAY TO LOCATE CANDIDATE DUPLICATE FILES.\n");
	printf ("      ALL MATCHES SHOULD VALIDATED BY ANOTHER MEANS PRIOR TO DELETION.\n");

	printf ("\n");
	duc_dir_close(dir);
	duc_close(duc);
	duc_del(duc);

	return 0;
}



struct cmd cmd_dup = {
	.name = "dup",
	.description = "List duplicates",
	.usage = "[options] [PATH]",
	.help = 
		"  -d, --database=ARG      use database file ARG [~/.duc.db]\n"
		"  -m, --megabytes=ARG     minimum filesize in megabytes to include in comparison\n"
		"  -q, --quiet             quiet mode, do not print any warnings\n"
		"  -f, --folderscan        return folder (directory level) matches\n"
		"  -i, --insensitive       enable case insensitive name or extension comparisons\n"
		"  -e, --extension         return matches for fileext and size = fileext and size\n"
		"  -s, --size              return matches for size = size\n"
		"  -n, --name              return matches for name = name\n"
		"  -h, --hash              perform additional hash check of disk files for matches (SLOW)\n"
		"\n"
		"\n"
		"Notes: \n"
		"    Name+Size matches always returned, then precedence is:\n"
		"         File Extension and Size match another item (requires -e) \n"
		"         Size match with another item (requires -s) \n"
		"         Name match with another item (requires -n) \n"
		"\n"
		"    Enabling matching at the folder level (-f) will invalidate \n"
		"         matching by file extension and size (-e) \n"
		"\n",

	.main = dup_main
};


/*
 * End
 */

