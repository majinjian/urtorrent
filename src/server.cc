/**
 * Implementation of connection class.
 *
 * Declaration "../include/connection.h". 
 *
 */

#include <server.h>
#include <netdb.h>        /* getaddrinfo() and struct addrinfo */
#include <arpa/inet.h>    /* inet_ntop() */
#include <unistd.h>       /* close() */

/**
 * Constructor - setup connection object as a P2P sender.
 *
 * @port: the local port number which the sender will listen.
 */
server::server(string port)
{
	this->port_ = port;
	this->setup();
}

/**
 * Establish P2P server, binding and listenning on port.
 */
void server::setup()
{
	addrinfo hint = {};     //hint info for getaddrinfo() , zero initialized
	addrinfo *result, *rp;  //address result
	int rv;                 //return val from getaddrinfo
	int yes = 1;            //option val for setsockopt()

	//assign address info to hint
	hint.ai_family = AF_INET;       //set for IPv4
	hint.ai_socktype = SOCK_STREAM; //TCP socket
	hint.ai_flags = AI_PASSIVE;     //server flag
	hint.ai_protocol = 0;

	//retrieve server address info to result
	rv = getaddrinfo(nullptr, this->port_.c_str(), &hint, &result);
	if (rv != 0) {
		error_handle(ERR_BIND);
	}

	//find appropriate address to bind
	for (rp = result; rp != nullptr; rp = rp->ai_next) {
		this->sockfd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (this->sockfd_ < 0) 
			continue;

		if (setsockopt(this->sockfd_, SOL_SOCKET, 
			             SO_REUSEADDR, &yes, sizeof(yes)) < 0)
			error_handle(ERR_SYS);

		if (bind(this->sockfd_, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		if (close(this->sockfd_) < 0)
			error_handle(ERR_SYS);
	}

	//no valid address found, error
	if(rp == nullptr) {
		error_handle(ERR_BIND);
	}

	//free result object
	freeaddrinfo(result);

	//start listen
	if(listen(this->sockfd_, server::QUEUE_LEN_) < 0)
		error_handle(ERR_SYS);
}

/**
 * Block waiting on a comming connection from another peer
 * @ip: string reference to place client's ip
 * Return: socket connected with remote peer
 */
int server::accept_peer(string& ip)
{
	sockaddr client_addr;                  //client address info
	socklen_t clien = sizeof(client_addr); //size of client address object

	//get new socket dealing with request
	int newsockfd = accept(this->sockfd_, &client_addr, &clien);

	if (newsockfd < 0)
		return newsockfd;

	//get client address
	struct sockaddr_in* addr = (struct sockaddr_in*)&client_addr;
	struct in_addr ipaddr = addr->sin_addr;
	char ip_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &ipaddr, ip_str, INET_ADDRSTRLEN);

  ip = string(ip_str);

	return newsockfd;
}
