/**
 * Class for urtorrent to act as a role of server.
 * The class will bind a given port and listenning on
 * it. It will handle incomming requests from peers.
 * The actuall optimistic choking is performed by this
 * class.
 *
 * Usage: - setup TCP server
 *        - accepting peer's request
 *        - dealing with file sharing and choking
 *
 */

#ifndef _SERVER_H_
#define _SERVER_H_

#include <string>         /* std::string */
#include <sys/socket.h>   /* socket syscalls */
#include <error_handle.h> /* error_handle() */

using namespace std;

/**
 * connection - class handle P2P TCP connections
 */
class server
{
	public:
		/* constructor */
		server(string port);
		/* incomming connection handler */
		int accept_peer(string& ip);
	
	private:
		int sockfd_;  /* socket file descriptor */
		string port_; /* connection port */
		//string dest_; /* remote server */
	
		static const int QUEUE_LEN_ = 5; /* TCP accept queue size */

		/* setup server TCP connection */
		void setup();
};
#endif
