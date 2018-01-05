/**
 * urotrrent - P2P file sharing client implementation.
 * 
 * Specific details are at <bittorrent.org>.
 *
 * Program launcher, performs following jobs:
 * - setup componets including:
 *   metainfo handle, tracker agent, TCP server,
 *   request dispatcher etc.
 *
 * - interact with user commands
 *
 * - cleanup objects on finish
 *
 */

#include <core.h>   /* Peer Wire Protocol core components */
#include <signal.h> /* signal() */

/***************** Constants *****************/
static const string PROMPT = "urtorrent> "; /* urtorrent command prompt */
static const string _EXIT = "quit";         /* quit command */
static const string _META = "metainfo";     /* metainfo command */
static const string _ANNOUCE = "announce";  /* announce command */
static const string _INFO = "trackerinfo";  /* trackerinfo command */
static const string _SHOW = "show";         /* show command */
static const string _STATUS = "status";     /* status command */

/************** Global Variables **************/
string port;          /* client port number */
string torrent;       /* torrent file */
string command;       /* user input command */
server* serv;         /* P2P sender */
metainfo* mi;         /* metainfo manipulator */
tracker_agent* agent; /* tracker local agent */
core* _core;          /* PWP control */
bool quit;            /* exit signal */


/************ Internal Functions **************/
void initialize();
void finalize();

/**
 * main - urtorrent driver function
 *
 * @argc: argument count
 * @argv: argument vector
 *
 * return: 0 on success termination, otherwise negative value is returned
 *
 */
int main(int argc, char **argv) 
{
	//input argument check
	if (argc != 3)
		error_handle(ERR_USAGE);

	//retrieve port and torrent from argument list
	port = argv[1];
	torrent = argv[2];

	//start up environments
	initialize();

	quit = false;
	//main loop
	while (!quit) {
		//print out prompt
		cout << PROMPT;
		cin >> command;

		if (command == _EXIT) {
			quit = true;
		}
		else if (command == _META) {
			mi->show_meta(agent->get_ip());
		}
		else if (command == _ANNOUCE) {
			agent->do_announce();
		}
		else if (command == _INFO) {
			agent->show_info(true);
		}
		else if (command == _SHOW) {
			_core->do_show();
		}
		else if (command == _STATUS) {
			_core->do_status();
		}
		else {
			help();
		}
	}

	//clean up
	finalize();
	return 0;
}

/**
 * Initialize urtorrent components
 */
void initialize()
{
	//ignore SIGPIPE signal
	signal(SIGPIPE, SIG_IGN);
	
	//establish P2P server
	serv = new server(port);

	//generate metainfo
	mi = new metainfo(torrent, port);

	//launch tracker agent
	agent = new tracker_agent(mi);

	//fire core functionality
	_core = new core(serv, mi, agent);
}

/**
 * Clean up objects
 */
void finalize() 
{
	delete _core;
	delete agent;
	delete mi;
	delete serv;
}
