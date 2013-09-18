/* -------------------------------------------------------------------------
 * diskd --- monitors shared disk.
 *   This applied pingd mechanism to disk monitor.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright (c) 2008 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 * -------------------------------------------------------------------------
 */

/**
  *  Ver.2.0  for Pacemaker 1.0.x
  */

#include <sys/param.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>
#include <string.h>

#include <clplumbing/Gmain_timeout.h>
#include <clplumbing/lsb_exitcodes.h>

#include <crm/crm.h>
#include <crm/common/util.h>
#include <crm/common/ipc.h>

#ifdef HAVE_GETOPT_H
#  include <getopt.h>
#endif

#include <glib.h>

#define MIN_INTERVAL 1
#define MAX_INTERVAL 3600
#define MIN_TIMEOUT 1
#define MAX_TIMEOUT 600
#define MIN_RETRY 	0
#define MAX_RETRY	10
#define MIN_RETRY_INTERVAL	1
#define MAX_RETRY_INTERVAL	3600
/* status */
#define ERROR 	1
#define normal 	-1
#define NONE  	2

#define BLKFLSBUF  _IO(0x12,97) /* flush buffer. refer linux/hs.h */
#define WRITE_DATA 64

#define WRITE_DIR "/tmp"
#define WRITE_FILE "diskcheck"
#define PID_FILE "/tmp/diskd.pid"

/* GMainLoop *mainloop = NULL; */
const char *crm_system_name = "diskd";

#define OPTARGS	"N:wd:a:i:p:DV?t:r:I:oe"

GMainLoop*  mainloop = NULL;
const char *diskd_attr = "diskd";
const char *attr_section = NULL;  /* for PM */
const char *attr_set = NULL;      /* for PM */

const char *device = NULL;  /* device name for disk check */
const char *wdir = NULL;
char *wfile = NULL;   /* directory name for disk check (write) 2008.10.24 */
gboolean wflag = FALSE;
int optflag = 0;  /* flag for duplicate */

int retry = 1;			/* disk check retry. default 1 times */
int retry_interval = 5; /* disk check retry intarval time. default 5sec. */
int interval = 30;  	/* disk check interval. default 30sec.*/
int timeout = 60; 		/* disk check read func timeout. default 60sec. */
int oneshot_flag = 0;  /* */
int exec_thread_flag = 0;  /* */
const char *diskcheck_value = NULL;
int pagesize = 0;
void *ptr = NULL;
void *buf;

static GMutex *diskd_mutex = NULL; 	/* Thread Mutex */
static GCond *diskd_cond = NULL;	/* Thread Cond */
static GMutex *thread_start_mutex = NULL; 	/* Thread Start Mutex */
static GCond *thread_start_cond = NULL;	/* Thread Start Cond */
static gboolean diskd_thread_use = FALSE;	/* Tthred Timer Flag */
static GThread *th_timer = NULL; 	/* Thread Timer */
static int timer_id = -1;
 
static void diskd_thread_timer_init(void);
static void diskd_thread_create(void);
static void diskd_thread_timer_variable_free(void);
static void diskd_thread_condsend(void);
static void diskd_thread_timer_end(void);
void send_update(void);

static gboolean
diskd_shutdown(int nsig, gpointer unused)
{
	crm_info("Exiting");
	
	if (timer_id != -1) {
		Gmain_timeout_remove(timer_id);
		timer_id = -1;
	}

	diskd_thread_condsend();

	if (mainloop != NULL && g_main_is_running(mainloop)) {
		g_main_quit(mainloop);
	} else {
		exit(0);
	}
	return FALSE;
}

static void
usage(const char *cmd, int exit_status)
{
	FILE *stream;

	stream = exit_status ? stderr : stdout;

	fprintf(stream, "usage: %s (-N|-w) [-daipDV?trIoe]\n", cmd);
	fprintf(stream, "    Basic options\n");
	fprintf(stream, "\t--%s (-%c) <devicename>\tDevice name to read\n"
		"\t\t\t\t\t* Required option\n", "read-device-name", 'N');
	fprintf(stream, "\t--%s (-%c) \t\t\tWrite check for disk, in %s/%s\n"
		"\t\t\t\t\t* Required option\n", "write-check", 'w', WRITE_DIR, WRITE_FILE);
	fprintf(stream, "\t--%s (-%c) <directoryname>\tDirectory Name to write\n"
		, "write-directory-name", 'd');
	fprintf(stream, "\t--%s (-%c) <string>\tName of the node attribute to set\n"
		"\t\t\t\t\t* Default=diskd\n", "attr-name", 'a');
	fprintf(stream, "\t--%s (-%c) <time[s]>\tDisk status check interval time\n"
		"\t\t\t\t\t* Default=30 sec.\n", "interval", 'i');
	fprintf(stream, "\t--%s (-%c) <filename>\tFile in which to store the process' PID\n"
		"\t\t\t\t\t* Default=%s\n", "pid-file", 'p', PID_FILE);
	fprintf(stream, "\t--%s (-%c) \t\tRun in daemon mode\n", "daemonize", 'D');
	fprintf(stream, "\t--%s (-%c) \t\t\tRun in verbose mode\n", "verbose", 'V');
	fprintf(stream, "\t--%s (-%c) \t\t\tDisk check one time\n", "oneshot", 'o');
	fprintf(stream, "\t--%s (-%c) \t\tCheck of the disk status check timeout by the thread\n"
		"\t\t\t\t\t* Default=60 sec.(Same value as check-timeout parameter)\n"
		"\t\t\t\t\t* Invalid at the time of the oneshot parameter designation\n", "exec-thread", 'e');
	fprintf(stream, "\t--%s (-%c) \t\t\tThis text\n", "help", '?');
	fprintf(stream, "    Note: -N, -w options cannot be specified at the same time.\n\n");
	fprintf(stream, "    Advanced options\n");
	fprintf(stream, "\t--%s (-%c) <time[s]>\tDisk status check timeout for select function\n"
		"\t\t\t\t\t* Default=60 sec.\n", "check-timeout", 't');
	fprintf(stream, "\t--%s (-%c) <times>\t\tDisk status check retry\n"
		"\t\t\t\t\t* Default=1 times\n", "retry", 'r');
	fprintf(stream, "\t--%s (-%c) <time[s]>\tDisk status check retry interval time\n"
		"\t\t\t\t\t* Default=5 sec.\n", "retry-interval", 'I');

	fflush(stream);

	exit(exit_status);
}

static gboolean
check_status(int new_status)
{
	if (oneshot_flag) { /* oneshot */
		return FALSE;
	}

	if (new_status != ERROR && new_status != normal) {
		crm_warn("non-defined status, new_status = %d", new_status);
		return FALSE;
	}

	if (diskd_thread_use == TRUE) {
		g_mutex_lock(diskd_mutex);
	}

	if (new_status == ERROR) {
		diskcheck_value = "ERROR";
		crm_warn("disk status is changed, attr_name=%s, target=%s, new_status=%s",
			diskd_attr, (wflag)? wdir : device, diskcheck_value);
	} else {
		diskcheck_value = "normal";
	}
	send_update();

	if (diskd_thread_use == TRUE) {
		g_mutex_unlock(diskd_mutex);
	}

	return TRUE;
}

static void diskd_thread_timer_init()
{
	if (exec_thread_flag) {
		if( ! g_thread_supported()) { 

			g_thread_init (NULL);

			
			thread_start_mutex = g_mutex_new();
			thread_start_cond = g_cond_new();
			diskd_mutex = g_mutex_new();
			diskd_cond = g_cond_new();
			if (diskd_mutex && diskd_cond && thread_start_mutex && thread_start_cond) {
				diskd_thread_use = TRUE;
			} else {
				diskd_thread_timer_variable_free();
				crm_warn("Failed in the generation of the thread variable. The thread timer is not available.");
			}
		} else {
			crm_warn("The thread timer of diskd is not supported. By this system, I/O blocking may occur by a check of diskd in read/write.");
		}
	}
}

static void diskd_thread_timer_variable_free()
{
	if (diskd_mutex != NULL) {
		g_mutex_free(diskd_mutex);
		diskd_mutex = NULL;
	}
	if (thread_start_mutex != NULL) {
		g_mutex_free(thread_start_mutex);
		thread_start_mutex = NULL;
	}

	if (diskd_cond != NULL) {
		g_cond_free(diskd_cond);
		diskd_cond = NULL;
	}

	if (thread_start_cond != NULL) {
		g_cond_free(thread_start_cond);
		thread_start_cond = NULL;
	}
}

static void diskd_thread_timer_end()
{
	if (diskd_thread_use == FALSE) return;


	diskd_thread_timer_variable_free();
}

static void diskd_thread_condsend()
{
	gpointer ret_thread;
	if (diskd_thread_use == FALSE) return;

	if (diskd_mutex && diskd_cond) {
		g_mutex_lock(diskd_mutex);
		g_cond_broadcast(diskd_cond);
		g_mutex_unlock(diskd_mutex);

		if (th_timer != NULL) {
			ret_thread = g_thread_join(th_timer);
			crm_debug_2("thread_join -> %d", GPOINTER_TO_INT (ret_thread));
			th_timer = NULL;
		}	
	} else {
		crm_warn("Cannot transmit cond to a thread");
	}

}

static void diskd_thread_timer_func(gpointer data)
{
	GTimeVal gtime;
	glong add_time = (timeout) * 1000 * 1000;
	gboolean bret;
	
	g_mutex_lock(thread_start_mutex);	

	/* Awaiting a start */
	g_mutex_lock(diskd_mutex);	

	/* A calculation of the waiting time and practice of the timer.(mergin 1s) */
	g_get_current_time(&gtime);
	g_time_val_add(&gtime, add_time); 

	g_cond_signal(thread_start_cond);

	g_mutex_unlock(thread_start_mutex);	

	bret = g_cond_timed_wait(diskd_cond, diskd_mutex, &gtime);
	g_mutex_unlock(diskd_mutex);	

	if (bret == FALSE){
		crm_warn("Timeout Error(s) occurred in diskd timer thread.");
		check_status(ERROR);
		g_thread_exit(GINT_TO_POINTER(ERROR));
	}

	crm_debug_2("Received Cond from Main().");
	g_thread_exit(GINT_TO_POINTER(normal));
}

static void diskd_thread_create()
{
	GError *gerr = NULL;

	if (diskd_thread_use) {

		g_mutex_lock(thread_start_mutex);
		
		if (th_timer == NULL) {
			th_timer = g_thread_create ((GThreadFunc)diskd_thread_timer_func, NULL, TRUE, &gerr);
			if (th_timer == NULL) {
				crm_err("Cannot create diskd timer_thread. %s", gerr->message);
				g_error_free(gerr);
				diskd_thread_use = FALSE;
				g_mutex_unlock(thread_start_mutex);	
			}
		}
			
		if (th_timer != NULL) {	
			g_cond_wait(thread_start_cond, thread_start_mutex);
			g_mutex_unlock(thread_start_mutex);	
		}
	}
}

static int diskcheck_wt(gpointer data)
{
	int fd = -1;
	int err, i;
	int select_err;
	struct timeval timeout_tv;
	fd_set write_fd_set;

	crm_debug_2("diskcheck_wt start");

	diskd_thread_create();

	for (i = 0; i <= retry; i++) {
		if ( i !=0 ) {
			sleep(retry_interval);
		}

		/* file open */
		fd = open(wfile, O_WRONLY | O_CREAT | O_DSYNC | O_NONBLOCK, 0);

		if (fd == -1) {
			crm_err("Could not open %s", wfile);
			cl_perror("%s", wfile);
			continue;  /* failed to open file. try re-open */
		}

		while( 1 ) {
			err = write(fd, buf, WRITE_DATA);  /* data write */
			if (err == WRITE_DATA) {
				crm_debug_2("data writing is OK");
				close(fd);
				if (-1 == remove((const char *)wfile)) {
					crm_warn("failed to remove file %s", wfile);
				}
				diskd_thread_condsend();
				check_status(normal);
				return normal;  /* OK */
			} else if (err != WRITE_DATA && errno == EAGAIN) {
				crm_warn("write function return errno:EAGAIN");
				FD_ZERO(&write_fd_set);
				FD_SET(fd, &write_fd_set);
				timeout_tv.tv_sec = timeout;
				timeout_tv.tv_usec = 0;
				select_err = select(fd+1, NULL, &write_fd_set, NULL,
					&timeout_tv);
				if (select_err == 1) {
					crm_warn("select ok, write again");
					continue;  /* retly write */
				} else if (select_err == -1) {
					crm_err("select failed on file %s", wfile);
					close(fd);
					if (-1 == remove((const char *)wfile)) {
						crm_warn("failed to remove file %s", wfile);
					}
					break;  /* failed to select */
				} else {
					crm_err("select time out on file %s", wfile);
					close(fd);
					if (-1 == remove((const char *)wfile)) {
						crm_warn("failed to remove file %s", wfile);
					}
					break;  /* failed to select */
				}
			} else {
				crm_err("Could not write to file %s", wfile);
				cl_perror("%s", wfile);
				close(fd);
				if (-1 == remove((const char *)wfile)) {
					crm_warn("failed to remove file %s", wfile);
				}
				break;  /* failed to write */
			}
		}
	}
	/* after for loop */

	diskd_thread_condsend();

	crm_warn("Error(s) occurred in diskcheck_wt function.");
	check_status(ERROR);

	return ERROR;

    /* file close */
}

static int diskcheck(gpointer data)
{
	int i;
	int fd = -1;
	int err;
	int select_err;
	struct timeval timeout_tv;
	fd_set read_fd_set;

	crm_debug_2("diskcheck start");

	diskd_thread_create();

	for (i = 0; i <= retry; i++) {
		if ( i !=0 ) {
			sleep(retry_interval);
		}

		fd = open((const char *)device, O_RDONLY | O_NONBLOCK, 0);
		if (fd == -1) {
			crm_err("Could not open device %s", device);
			continue;
		}

		err = ioctl(fd, BLKFLSBUF, 0);
		if (err != 0) {
			crm_err("ioctl error, Could not flush buffer");
			close(fd);
			continue;
		}

		while( 1 ) {
			err = read(fd, buf, pagesize);
			if (err == pagesize) {
				crm_debug_2("reading form data is OK");
				close(fd);
				diskd_thread_condsend();
				check_status(normal);
				return normal;
			} else if (err != pagesize && errno == EAGAIN) {
				crm_warn("read function return errno:EAGAIN");
				FD_ZERO(&read_fd_set);
				FD_SET(fd, &read_fd_set);
				timeout_tv.tv_sec = timeout;
				timeout_tv.tv_usec = 0;
				select_err = select(fd+1, &read_fd_set, NULL, NULL, &timeout_tv);
				if (select_err == 1) {
					crm_warn("select ok, read again");
					continue;
				} else if (select_err == -1) {
					crm_err("select failed on device %s", device);
					close(fd);
					break;
				}
			} else {
				crm_err("Could not read from device %s", device);
				close(fd);
				break;
			}
		}
	}

	diskd_thread_condsend();

	crm_warn("Error(s) occurred in diskcheck function.");
	check_status(ERROR);

	return ERROR;
}


static int oneshot(void)
{
	int rc = 0;

        if ( wflag ) {  /* writer */
                if (wfile == NULL) {
			wdir = crm_strdup(WRITE_DIR);
                        crm_malloc0(wfile, PATH_MAX);
                        g_snprintf(wfile, PATH_MAX, "%s/%s", WRITE_DIR, WRITE_FILE);
                }
                buf = (void *)malloc(WRITE_DATA);
                if (buf == NULL) {
                        crm_err("Could not allocate memory");
                        exit(LSB_EXIT_GENERIC);
                }
                rc = diskcheck_wt(NULL);
		crm_free(wfile);
        } else {                /* reader */
                pagesize = getpagesize();
                ptr = (void *)malloc(2 * pagesize);
                if (ptr == NULL) {
                        crm_err("Could not allocate memory");
                        exit(LSB_EXIT_GENERIC);
                }
                buf = (void *)(((u_long)ptr + pagesize) & ~(pagesize-1));
                rc = diskcheck(NULL);
		free(ptr);
        }

	if (rc == ERROR) {
		return ERROR;
	}

	return 0;
}


int
main(int argc, char **argv)
{
	int argerr = 0;
	int flag;
	char *pid_file = NULL;
	gboolean daemonize = FALSE;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		/* Top-level Options */
		{"verbose", 0, 0, 'V'},
		{"help", 0, 0, '?'},
		{"pid-file",  1, 0, 'p'},
		{"attr-name", 1, 0, 'a'},
		{"read-device-name",  1, 0, 'N'},
		{"daemonize", 0, 0, 'D'},
		{"interval",  1, 0, 'i'},
		{"retry", 1, 0, 'r'},
		{"retry-interval", 1, 0, 'I'},
		{"check-timeout", 1, 0, 't'},
		{"write-check", 0, 0, 'w'},   	    /* add option 2008.10.24 */
		{"write-directory-name", 1, 0, 'd'},   	    /* add option 2009.4.17 */
		{"oneshot", 0, 0, 'o'},   	/* add option 2009.10.01 */
		{"exec-thread", 0, 0, 'e'},   	/* add option 2011.09.30 */

		{0, 0, 0, 0}
	};
#endif
	pid_file = crm_strdup(PID_FILE);
	crm_system_name = basename(argv[0]);

	G_main_add_SignalHandler(
		G_PRIORITY_HIGH, SIGTERM, diskd_shutdown, NULL, NULL);

	crm_log_init(basename(argv[0]), LOG_INFO, TRUE, FALSE, argc, argv);

	/* check user. user shuld be root.*/
	if (strcmp("root", (const gchar *)g_get_user_name()) != 0) {
		crm_err("permission denied. diskd should be executed by root.\n");
		printf ("permission denied. diskd should be executed by root.\n");
		exit(LSB_EXIT_GENERIC);
	}

	while (1) {
#ifdef HAVE_GETOPT_H
		flag = getopt_long(argc, argv, OPTARGS,
				   long_options, &option_index);
#else
		flag = getopt(argc, argv, OPTARGS);
#endif
		if (flag == -1)
			break;

		switch(flag) {
			case 'V':
				cl_log_enable_stderr(TRUE);
				alter_debug(DEBUG_INC);
				break;
			case 'p':
				crm_free(pid_file);
				pid_file = crm_strdup(optarg);
				break;
			case 'a':
				diskd_attr = crm_strdup(optarg);
				break;
			case 'r':
				retry = crm_parse_int(optarg, "1");
				if ((retry == 0) && (strcmp(optarg, "0") != 0)) {
					argerr++;
					break;
				}
				if ((retry < MIN_RETRY) || (retry > MAX_RETRY))
					++argerr;
				break;
			case 'I':
				retry_interval = crm_parse_int(optarg, "1");
				if ((retry_interval < MIN_RETRY_INTERVAL) || (retry_interval > MAX_RETRY_INTERVAL))
					++argerr;
				break;
			case 'i':
				interval = crm_parse_int(optarg, "1");
				if ((interval < MIN_INTERVAL) || (interval > MAX_INTERVAL))
					++argerr;
				break;
			case 't':
				timeout = crm_parse_int(optarg, "1");
				if ((timeout < MIN_TIMEOUT) || (timeout > MAX_TIMEOUT))
					++argerr;
				break;
			case 'N':
				device = crm_strdup(optarg);
				optflag++; /* add 2008.20.24 */
				break;
			case 'D':
				daemonize = TRUE;
				break;
			case 'w':   /* add option 2008.10.24 */
				wflag = TRUE;
				optflag++;
				break;
			case 'd':   /* add option 2009.4.17 */
				wdir = crm_strdup(optarg);
				crm_malloc0(wfile, PATH_MAX);
				g_snprintf(wfile, PATH_MAX, "%s/%s", optarg, WRITE_FILE);
				break;
			case 'o':   /* add option 2009.10.01 */
				oneshot_flag =1;
				break;
			case 'e':   /* add option 2011.09.30 */
				exec_thread_flag =1;
				break;
			case '?':
				usage(crm_system_name, LSB_EXIT_GENERIC);
				break;
			default:
				printf ("Argument code 0%o (%c) is not (?yet?) supported\n", flag, flag);
				crm_err("Argument code 0%o (%c) is not (?yet?) supported\n", flag, flag);
				++argerr;
				break;
		}
	}


	if (optind < argc) {
		crm_err("non-option ARGV-elements: ");
		printf ("non-option ARGV-elements: ");
		while (optind < argc) {
			crm_err("%s ", argv[optind]);
			printf("%s ", argv[optind]);
			optind++;
		}
		printf("\n");
		argerr ++;
	}
	if ((argerr) || (optflag >= 2) || (device == NULL && wflag == FALSE)) {  /* add optflag 2008.10.24 */
		/* "-N" + "-w" pattern and not "-N" + not "-w"*/
		usage(crm_system_name, LSB_EXIT_GENERIC);
	}
	if ((device != NULL) && (wfile != NULL)) {
		/* "-N" + "-d" pattern */
		crm_warn("\"d\" option was ignored, because N option was specified.");
	}

	if (oneshot_flag) {
		int rc = 0;

		crm_free(pid_file);
		rc = oneshot();
		exit (rc);
	}

	crm_make_daemon(crm_system_name, daemonize, pid_file);
	diskd_thread_timer_init();

	if ( wflag ) {	/* writer */
		if (wfile == NULL) {
			wdir = crm_strdup(WRITE_DIR);
			crm_malloc0(wfile, PATH_MAX);
			g_snprintf(wfile, PATH_MAX, "%s/%s", WRITE_DIR, WRITE_FILE);
		}
		buf = (void *)malloc(WRITE_DATA);
		if (buf == NULL) {
			crm_err("Could not allocate memory");
			check_status(ERROR);
			exit(LSB_EXIT_GENERIC);
		}
		diskcheck_wt(NULL);
		timer_id = Gmain_timeout_add(interval*1000, diskcheck_wt, NULL);
	} else {		/* reader */
		pagesize = getpagesize();
		ptr = (void *)malloc(2 * pagesize);
		if (ptr == NULL) {
			crm_err("Could not allocate memory");
			check_status(ERROR);
			exit(LSB_EXIT_GENERIC);
		}
		buf = (void *)(((u_long)ptr + pagesize) & ~(pagesize-1));
		diskcheck(NULL);
		timer_id = Gmain_timeout_add(interval*1000, diskcheck, NULL);
	}

	crm_info("Starting %s", crm_system_name);
	mainloop = g_main_new(FALSE);
	g_main_run(mainloop);

	free(ptr);
	crm_free(pid_file);
	if (wfile != NULL) {
		crm_free(wfile);
	}

	diskd_thread_timer_end();

	crm_info("Exiting %s", crm_system_name);
	return 0;
}

void
send_update(void)
{
	attrd_lazy_update('U', NULL, diskd_attr, diskcheck_value, attr_section, attr_set, "0");
}


