/*
 * This file is part of harddns.
 *
 * (C) 2019 by Sebastian Krahmer, sebastian [dot] krahmer [at] gmail [dot] com
 *
 * harddns is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * harddns is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with harddns. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <iostream>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pwd.h>
#include <grp.h>
#include "config.h"
#include "proxy.h"
#include "init.h"


using namespace std;
using namespace harddns;


void close_fds()
{
	struct rlimit rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
		perror("Can't close files, getrlimit:");
		exit(-1);
	}
	if (rl.rlim_max == RLIM_INFINITY)
		rl.rlim_max = 4096;
	for (unsigned int i = 0; i <= rl.rlim_max; ++i)
		close(i);
	open("/dev/null", O_RDWR);
	dup2(0, 1);
	dup2(0, 2);
}


int main(int argc, char **argv)
{
	char c = 0;
	string laddr = "127.0.0.1", lport = "53", root = "/", user = "nobody";

	while ((c = getopt(argc, argv, "l:p:R:u:")) != -1) {

		switch (c) {
		case 'l':
			laddr = optarg;
			break;
		case 'p':
			lport = optarg;
			break;
		case 'R':
			root = optarg;
			break;
		case 'u':
			user = optarg;
			break;
		default:
			break;
		}
	}

	struct passwd *pw = getpwnam(user.c_str());
	if (!pw) {
		cerr<<"Unknown user '"<<user<<"'. Exiting.\n";
		return -1;
	}
	uid_t user_uid = pw->pw_uid;
	gid_t user_gid = pw->pw_gid;

	if (fork() > 0)
		return 0;

	close_fds();
	setsid();

	harddns_init();

	doh_proxy doh;

	if (doh.init(laddr, lport) < 0) {
		syslog(LOG_INFO, "%s", doh.why());
		harddns_fini();
		return -1;
	}

	// Must happen before chroot()
	if (initgroups(user.c_str(), user_gid) < 0) {
		syslog(LOG_INFO, "initgroups: %s", strerror(errno));
		harddns_fini();
		return -1;
	}

	if (chdir(root.c_str()) < 0 || chroot(root.c_str()) < 0) {
		syslog(LOG_INFO, "Failed to chroot: %s", strerror(errno));
		harddns_fini();
		return -1;
	}

	if (setgid(user_gid) < 0 || setuid(user_uid) < 0) {
		syslog(LOG_INFO, "Failed to setuid to user '%s': %s", user.c_str(), strerror(errno));
		harddns_fini();
		return -1;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGHUP, &sa, nullptr) < 0 || sigaction(SIGPIPE, &sa, nullptr) < 0) {
		syslog(LOG_INFO, "Failed to setup signal handlers: %s", strerror(errno));
		harddns_fini();
		return -1;
	}

	if (doh.loop() < 0)
		syslog(LOG_INFO, "%s", doh.why());

	harddns_fini();

	return -1;
}
