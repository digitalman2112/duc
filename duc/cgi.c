
#include "config.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>
#include <signal.h>

#include "cmd.h"
#include "duc.h"
#include "duc-graph.h"


/*
 * Simple parser for CGI parameter line. Does not excape CGI strings.
 */

struct param {
	char *key;
	char *val;
	struct param *next;
};

struct param *param_list = NULL;

int opt_list_dir = 0;
int opt_allow_index = 0;
int opt_pixels = 1000;
int opt_max_levels = 5;
int opt_tooltips = 0;


int decodeURIComponent (char *sSource, char *sDest) {
	int nLength;
	for (nLength = 0; *sSource; nLength++) {
		if (*sSource == '%' && sSource[1] && sSource[2] && isxdigit(sSource[1]) && isxdigit(sSource[2])) {
			sSource[1] -= sSource[1] <= '9' ? '0' : (sSource[1] <= 'F' ? 'A' : 'a')-10;
			sSource[2] -= sSource[2] <= '9' ? '0' : (sSource[2] <= 'F' ? 'A' : 'a')-10;
			sDest[nLength] = 16 * sSource[1] + sSource[2];
			sSource += 3;
			continue;
		}
		sDest[nLength] = *sSource++;
	}
	sDest[nLength] = '\0';
	return nLength;
}

static int cgi_parse(void)
{
	char *qs = getenv("QUERY_STRING");
	if(qs == NULL) return -1;
	decodeURIComponent(qs,qs);

	char *p = qs;

	for(;;) {

		char *pe = strchr(p, '=');
		if(!pe) break;
		char *pn = strchr(pe, '&');
		if(!pn) pn = pe + strlen(pe);

		char *key = p;
		int keylen = pe-p;
		char *val = pe+1;
		int vallen = pn-pe-1;

		struct param *param = malloc(sizeof(struct param));
		assert(param);

		param->key = malloc(keylen+1);
		assert(param->key);
		strncpy(param->key, key, keylen);

		param->val = malloc(vallen+1);
		assert(param->val);
		strncpy(param->val, val, vallen);
		
		param->next = param_list;
		param_list = param;

		if(*pn == 0) break;
		p = pn+1;
	}

	return 0;
}


static char *cgi_get(const char *key)
{
	struct param *param = param_list;

	while(param) {
		if(strcmp(param->key, key) == 0) {
			return param->val;
		}
		param = param->next;
	}

	return NULL;
}


static void do_JSON(duc *duc, duc_graph *graph, duc_dir *dir)
{
	char *script = getenv("SCRIPT_NAME");
        if(!script) return;

	char *qs = getenv("QUERY_STRING");

        int x = 0, y = 0;
        if(qs) {
                char *p1 = strchr(qs, '?');
                if(p1) {
                        char *p2 = strchr(p1, ',');
                        if(p2) {
                                x = atoi(p1+1);
                                y = atoi(p2+1);
                        }
                }
        }

    	printf("Content-Type: application/json\n\n");
        printf("{\n");

	if(x || y) {
                duc_dir *dir2 = duc_graph_find_spot(graph, dir, x, y);
                if (dir2) {
			printf("\"path\":\"%s\",\n", duc_dir_get_path(dir2));
			printf("\"size\":\"%s\",\n", duc_human_size(duc_dir_get_size(dir2)));
			printf("\"count\":\"%ld\",\n", duc_dir_get_count(dir2));

			char url[PATH_MAX];
	                snprintf(url, sizeof url, "\"%s?cmd=index&path=%s\"\n", script, duc_dir_get_path(dir2));
			printf("\"url\":%s", url);
			}
		else {
			printf("\"error\":\"No Path at Coordinates\"\n");
		}
         }
        printf("}\n");

	return;

}

static void do_index(duc *duc, duc_graph *graph, duc_dir *dir)
{
	char *path = cgi_get("path");
	char *script = getenv("SCRIPT_NAME");
	if(!script) return;

	char *qs = getenv("QUERY_STRING");
	fprintf(stderr,"querystring=%s\n", qs); 
	int x = 0, y = 0;
	if(qs) {
		char *p1 = strchr(qs, '?');
		if(p1) {
			char *p2 = strchr(p1, ',');
			if(p2) {
				x = atoi(p1+1);
				y = atoi(p2+1);
			}
		}
	}

	if(x || y) {
		duc_dir *dir2 = duc_graph_find_spot(graph, dir, x, y);
		if (dir2) {
			 path = duc_dir_get_path(dir2);
		}

		char redirURL[PATH_MAX];
		snprintf(redirURL, sizeof redirURL, "%s?cmd=index&path=%s", script, path);

		printf(
                	"Content-Type: text/html\n"
                	"\n"
                	"<!DOCTYPE html>\n"
                	"<head>\n"
			);
		printf("<meta http-equiv='refresh' content='0; URL=%s'>\n", redirURL);
		printf("</head>\n");
		printf("</html>\n");
		return; //todo: cleanup
	}


	 printf(
                "Content-Type: text/html\n"
                "\n"
                "<!DOCTYPE html>\n"
                "<head>\n"
                "<style>\n"
                "body { font-family: 'arial', 'sans-serif'; font-size: 11px; }\n"
                "table, thead, tbody, tr, td, th { font-size: inherit; font-family: inherit; }\n"
                "#list { 100%%; }\n"
                "#list td { padding-left: 5px; }\n"
                "</style>\n"
		);

	if (opt_tooltips) {
		printf(
 			"<script src='//code.jquery.com/jquery-1.11.2.js'></script>\n"

  			"<!--tooltipster-->\n"
  			"<link rel='stylesheet' type='text/css' href='/tooltipster.css' />\n"
  			"<link rel='stylesheet' type='text/css' href='/tooltipster-duc.css' />\n"
  			"<script type='text/javascript' src='/jquery.tooltipster.min.js'></script>\n"
		);
	}

	printf("</head>\n");

	struct duc_index_report *report;
	int i = 0;

	printf("<body>");
	printf("<center>");

	printf("<table id=list>");
	printf("<tr>");
	printf("<th>Path</th>");
	printf("<th>Size</th>");
	printf("<th>Files</th>");
	printf("<th>Directories</th>");
	printf("<th>Date</th>");
	printf("<th>Time</th>");
	printf("</tr>");

	while( (report = duc_get_report(duc, i)) != NULL) {

		char ts_date[32];
		char ts_time[32];
		struct tm *tm = localtime(&report->time_start.tv_sec);
		strftime(ts_date, sizeof ts_date, "%Y-%m-%d",tm);
		strftime(ts_time, sizeof ts_time, "%H:%M:%S",tm);

		char url[PATH_MAX];
		char reindex_url[PATH_MAX];

		snprintf(url, sizeof url, "%s?cmd=index&path=%s", script, report->path);
		snprintf(reindex_url, sizeof reindex_url, "%s?cmd=reindex&path=%s", script, report->path);

		char *siz = duc_human_size(report->size_total);

		printf("<tr>");
		printf("<td><a href='%s'>%s</a></td>", url, report->path);
		printf("<td>%s</td>", siz);
		printf("<td>%lu</td>", (unsigned long)report->file_count);
		printf("<td>%lu</td>", (unsigned long)report->dir_count);
		printf("<td>%s</td>", ts_date);
		printf("<td>%s</td>", ts_time);
		if (opt_allow_index) {
			printf("<td><a href='%s'>%s</a></td>", reindex_url, "reindex");
		}
		printf("</tr>\n");

		free(siz);

		duc_index_report_free(report);
		i++;
	}
	printf("</table>");
	
	fflush(stdout);

	if (path) {
	 

		printf("<a href='%s?cmd=index&path=%s&'>", script, path);
		printf("<img src='%s?cmd=image&path=%s' ismap='ismap' id='target'>\n", script, path);
		printf("</a><br><table>");
	
		if (opt_list_dir) {
			//this code is based on ls.c ls_one()	
			size_t n = 0;
			int max_size_len = 8; //this is calculated in ls_one

			struct duc_dirent *e;

			while( (e = duc_dir_read(dir)) != NULL) {
		
		
				char *siz = duc_human_size(e->size);
				printf("<tr><td align='right'>%*s</td>", max_size_len, siz);
				free(siz);

				if (e->mode == DUC_MODE_DIR) {
					printf("<td><a href='%s?cmd=index&path=%s/%s&'>", script, path, e->name);
					printf("%s</a></td>", e->name);
				}
				else {
					printf("<td>%s</td>", e->name);
				}

				n++;
			}

		printf("</table>");
		} //end if (opt_list_dir)	
	} //end if (path)
	printf("<br><br><br><h2>Disk Usage Charts by <a href='https://github.com/zevv/duc'>DUC</a></h2><br> ");
	printf("<script type='text/javascript' src='/duc.js'></script>");

	printf("</body>");

	fflush(stdout);
}


void do_image(duc *duc, duc_graph *graph, duc_dir *dir)
{
	printf("Content-Type: image/png\n");
	printf("\n");

	if(dir) {
		duc_graph_draw_file(graph, dir, DUC_GRAPH_FORMAT_PNG, stdout);
	}
}

void do_reindex(duc *duc, duc_graph *graph, duc_dir *dir)
{
	setvbuf(stdout, NULL, _IONBF, 0); //disable buffering 
        printf(
                "Content-Type: text/html\n"
                "\n"
                "<!DOCTYPE html>\n"
                "<head>\n"
                "<style>\n"
                "body { font-family: 'arial', 'sans-serif'; font-size: 11px; }\n"
                "table, thead, tbody, tr, td, th { font-size: inherit; font-family: inherit; }\n"
                "#list { 100%%; }\n"
                "#list td { padding-left: 5px; }\n"
                "</style>\n"
                "</head>\n"
        );

	
        char *path = cgi_get("path");
	char *script = getenv("SCRIPT_NAME");
	if(!script) return;

  	char url[PATH_MAX];
        snprintf(url, sizeof url, "%s?cmd=index&path=%s", script, path);

	printf("<body><h1>Starting index of: <a href='%s'>%s</a></h1><br><br>", url ,path);
	printf("<h2>This operation can take minutes on large paths, please be patient.</h2>\n");
	printf("<h2>The indexing should continue even if this window is closed, however there will be no notification.</h2>\n");
	fflush(stdout);

	duc_index_req *req = duc_index_req_new(duc);

	//dup2(1, 2); //set stderror to the browser

	signal(SIGABRT,SIG_IGN); //keep running if nav away from page
	struct duc_index_report *report;
	report = duc_index(req, path, DUC_INDEX_XDEV); 
 	if(report == NULL) {
		printf("%s\n", duc_strerror(duc));
	}
	
	char *siz = duc_human_size(report->size_total);
	char *s = duc_human_duration(report->time_start, report->time_stop);
	printf("<h2>Indexed %lu files and %lu directories, (%sB total) in %s\n</h2>", 
			(unsigned long)report->file_count, 
			(unsigned long)report->dir_count,
			siz,
			s);	
	free(s);
	free(siz);

	duc_index_report_free(report);

	printf("<a href='%s?cmd=index&path=%s&'>", script, path);
        printf("<img src='%s?cmd=image&path=%s' ismap='ismap'>\n", script, path);
        printf("</a><br>");
	printf("</body>");
	fflush(stdout);

}


static int cgi_main(int argc, char **argv)
{
	int r;

	r = cgi_parse();
	if(r != 0) {
		fprintf(stderr,
			"The 'cgi' subcommand is used for integrating Duc into a web server.\n"
			"Please refer to the documentation for instructions how to install and configure.\n"
		);
		return(-1);
	}

	char *path_db = NULL;

	struct option longopts[] = {
		{ "database",       required_argument, NULL, 'd' },
		{ "maxlevels",      required_argument, NULL, 'm' },
		{ "pixels",         required_argument, NULL, 'p' },
		{ "list",           optional_argument, NULL, 'l' },
		{ "index",          optional_argument, NULL, 'i' },
		{ "tooltips",       optional_argument, NULL, 't' },
		{ NULL }
	};

	int c;
	while( ( c = getopt_long(argc, argv, "d:m:p:lit", longopts, NULL)) != EOF) {

		switch(c) {
			case 'd':
				path_db = optarg;
				break;
			case 'l':
				opt_list_dir = 1;
				break;
			case 'i':
				opt_allow_index = 1;
				break;
			case 'p':
				if (atol(optarg)) {
					opt_pixels = atol(optarg);
				}
				break;
			case 'm':
				if (atol(optarg)) {
					opt_max_levels = atol(optarg);
				}
				break;
			case 't':
				opt_tooltips=1;
				break;
			default:
				return -2;
		}
	}


	char *cmd = cgi_get("cmd");
	if(cmd == NULL) cmd = "index";

	duc *duc = duc_new();
	if(duc == NULL) {
		printf("Content-Type: text/plain\n\n");
                printf("Error creating duc context\n");
		return -1;
        }

	duc_set_log_level(duc, DUC_LOG_WRN);


	duc_open_flags openflag = DUC_OPEN_RO;
	if(strcmp(cmd, "reindex")==0) {
		openflag = DUC_OPEN_RW;
		}

        r = duc_open(duc, path_db, openflag); 
        if(r != DUC_OK) {
		printf("Content-Type: text/plain\n\n");
                printf("%s\n", duc_strerror(duc));
		return -1;
        }

	duc_dir *dir = NULL;
	char *path = cgi_get("path");
	if(path) {
		dir = duc_dir_open(duc, path);
		if(dir == NULL) {
			fprintf(stderr, "%s\n", duc_strerror(duc));
			return 0;
		}
	}

	duc_graph *graph = duc_graph_new(duc);
	duc_graph_set_size(graph, opt_pixels);
	duc_graph_set_max_level(graph, opt_max_levels);

	if(strcmp(cmd, "index") == 0) do_index(duc, graph, dir);
	if( (strcmp(cmd, "reindex") == 0) && opt_allow_index) do_reindex(duc, graph, dir);
	if(strcmp(cmd, "image") == 0) do_image(duc, graph, dir);
	if(strcmp(cmd, "JSON") == 0) do_JSON(duc, graph, dir);

	if(dir) duc_dir_close(dir);
	duc_close(duc);
	duc_del(duc);

	return 0;
}


struct cmd cmd_cgi = {
	.name = "cgi",
	.description = "CGI interface",
	.usage = "[options] [PATH]",
	.help =
		"  -d, --database=ARG      use database file ARG [~/.duc.db]\n"
		"  -i, --index		   allow index commands\n"
		"  -l, --list		   turn on directory listing\n"
		"  -m, --maxlevels=ARG	   set max levels\n"
		"  -p, --pixels=ARG	   set image size in pixels.\n"
		"  -t, --tooltips	   turn on tooltips (requires www files)\n",
	.main = cgi_main,

};


/*
 * End
 */

