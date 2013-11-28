/* 
 * 	Copyright (c) 2010 Colorado State University
 * 
 *	Permission is hereby granted, free of charge, to any person
 *	obtaining a copy of this software and associated documentation
 *	files (the "Software"), to deal in the Software without
 *	restriction, including without limitation the rights to use,
 *	copy, modify, merge, publish, distribute, sublicense, and/or
 *	sell copies of the Software, and to permit persons to whom
 *	the Software is furnished to do so, subject to the following
 *	conditions:
 *
 *	The above copyright notice and this permission notice shall be
 *	included in all copies or substantial portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *	OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *	WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *	OTHER DEALINGS IN THE SOFTWARE.\
 * 
 * 
 *  File: mrtcontrol.c
 * 	Authors: He Yan, Dan Massey
 *
 *  Date: Oct 7, 2008 
 */
 
/* 
 * Control and manage the quaaga connections
 */

/* externally visible structures and functions for mrts */
#include "mrt.h"
/* internal structures and functions for this module */
#include "mrtcontrol.h"
/* internal structures and functions for launching mrts */
#include "mrtinstance.h"

/* required for logging functions */
#include "../Util/log.h"
/* needed for reading and saving configuration */
#include "../Config/configdefaults.h"
#include "../Config/configfile.h"

/* required for TRUE/FALSE defines  */
#include "../Util/bgpmon_defaults.h"

/* needed for address management  */
#include "../Util/address.h"

/* needed for checkACL */
#include "../Util/acl.h"

/* needed for label action definition */
#include "../Labeling/label.h"

/* needed for malloc and free */
#include <stdlib.h>
/* needed for strncpy */
#include <string.h>
/* needed for system error codes */
#include <errno.h>
/* needed for addrinfo struct */
#include <netdb.h>
/* needed for system types such as time_t */
#include <sys/types.h>
/* needed for time function */
#include <time.h>
/* needed for socket operations */
#include <sys/socket.h>
/* needed for pthread related functions */
#include <pthread.h>
/* needed for sleep and close */
#include <unistd.h>

/* needed for checkACL */
#include "../Peering/peers.h"

//#define DEBUG

/*--------------------------------------------------------------------------------------
 * Purpose: Initialize the default mrt control configuration.
 * Input: none
 * Output: returns 0 on success, 1 on failure
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
initMrtControlSettings()
{
	int err = 0;

	// address used to listen for mrt connections
	int result = checkAddress(MRT_LISTEN_ADDR, ADDR_PASSIVE);
	if(result != ADDR_VALID)
	{
		err = 1;
		strncpy(MrtControls.listenAddr, IPv4_ANY, ADDR_MAX_CHARS);
	}
	else
		strncpy(MrtControls.listenAddr, MRT_LISTEN_ADDR, ADDR_MAX_CHARS);

	// port used to listen for mrt connections
	if ( (MRT_LISTEN_PORT < 1) || (MRT_LISTEN_PORT > 65536) ) 
	{
		err = 1;
		log_warning("Invalid site default for mrt listen port.");
		MrtControls.listenPort= 7777;
	}
	else
		MrtControls.listenPort= MRT_LISTEN_PORT;

	// mrt connections enabled
	if ( (MRT_LISTEN_ENABLED != TRUE) && (MRT_LISTEN_ENABLED != FALSE) )
	{
		err = 1;
		log_warning("Invalid site default for mrts enabled.");
		MrtControls.enabled= FALSE;
	}
	else
		MrtControls.enabled= MRT_LISTEN_ENABLED;

	// Maximum number of mrt connections allowed
	if (MAX_MRTS_IDS < 0)  
	{
		err = 1;
		log_warning("Invalid site default for max allowed mrts.");
		MrtControls.maxMrts = 100;
	}
	else
		MrtControls.maxMrts = MAX_MRTS_IDS;

	// The label action for mrts
	if (MRT_LABEL_ACTION < NoAction || MRT_LABEL_ACTION > StoreRibOnly)  
	{
		err = 1;
		log_warning("Invalid site default for qugga label action.");
		MrtControls.labeAction = Label;
	}
	else
		MrtControls.labeAction = MRT_LABEL_ACTION;

	// initial bookkeeping figures
	MrtControls.activeMrts = 0;
	MrtControls.nextMrtID = 1;
	MrtControls.rebindFlag = FALSE;
	MrtControls.shutdown = FALSE;
	MrtControls.lastAction = time(NULL);
	MrtControls.firstNode = NULL;

	// create a lock for the mrts list
	if (pthread_mutex_init( &(MrtControls.mrtLock), NULL ) )
		log_fatal( "unable to init mutex lock for mrts");

	return err;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Read the mrt control settings from the config file.
 * Input: none
 * Output:  returns 0 on success, 1 on failure
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
readMrtControlSettings()
{	
	int 	err = 0;
	int 	result;
	int 	num;
	char	*addr;

	// get listen addr
	result = getConfigValueAsAddr(&addr, XML_MRTS_CTR_LISTEN_ADDR_PATH, ADDR_PASSIVE);
	if (result == CONFIG_VALID_ENTRY) 
	{
		result = checkAddress(addr, ADDR_PASSIVE);
		if(result != ADDR_VALID)
		{
			err = 1;
			log_warning("Invalid configuration of mrts listen address.");
		}
		else 
		{
			strncpy(MrtControls.listenAddr,addr,ADDR_MAX_CHARS);
			free(addr);
                        addr = NULL;
		}
	}
	else if ( result == CONFIG_INVALID_ENTRY ) 
	{
	  	err = 1;
		log_warning("Invalid configuration of mrts listen address.");
	}
	else
		log_msg("No configuration of mrts listen address, using default.");
#ifdef DEBUG
		debug(__FUNCTION__, "Mrt Control Addr:%s", MrtControls.listenAddr);
#endif

	// get listen port
	result = getConfigValueAsInt(&num, XML_MRTS_CTR_LISTEN_PORT_PATH,1,65536);
	if (result == CONFIG_VALID_ENTRY) 
		MrtControls.listenPort = num;
	else if( result == CONFIG_INVALID_ENTRY ) 
	{
	  	err = 1;
		log_warning("Invalid configuration of mrts listen port.");
	}
	else
		log_msg("No configuration of mrts listen port, using default.");
#ifdef DEBUG
		debug(__FUNCTION__, "Mrts listen port set to %d", MrtControls.listenPort);
#endif

	// get enabled status of mrts control module
	result = getConfigValueAsInt(&num, XML_MRTS_CTR_ENABLED_PATH, 0, 1);
	if (result == CONFIG_VALID_ENTRY) 
		MrtControls.enabled = num;
	else if ( result == CONFIG_INVALID_ENTRY ) 
	{
	  	err = 1;
		log_warning("Invalid configuration of mrts listen enabled.");
	}
	else
		log_msg("No configuration of mrts listen enabled, using default.");
#ifdef DEBUG
	if (num == TRUE) 
		debug(__FUNCTION__, "Mrts listen enabled");
	else
		debug(__FUNCTION__, "Mrts listen disabled");
#endif

	// get the max number of mrts
	result = getConfigValueAsInt(&num, XML_MRTS_CTR_MAX_MRTS_PATH, 0, 65536);
	if (result == CONFIG_VALID_ENTRY) 
		MrtControls.maxMrts = num;	
	else if ( result == CONFIG_INVALID_ENTRY ) 
	{
	  	err = 1;
		log_warning("Invalid configuration of max mrts.");
        }
        else
                log_msg("No configuration of the number of max mrts, using default.");
#ifdef DEBUG
	debug(__FUNCTION__, "Maximum mrts allowed is %d", MrtControls.maxMrts);
#endif

	// get enabled status of mrts control module
	result = getConfigValueAsInt(&num, XML_MRTS_CTR_LABEL_ACTION_PATH, NoAction, StoreRibOnly);
	if (result == CONFIG_VALID_ENTRY) 
		MrtControls.labeAction = num;
	else if ( result == CONFIG_INVALID_ENTRY ) 
	{
		err = 1;
		log_warning("Invalid configuration of mrts label action.");
	}
	else
		log_msg("No configuration of mrts label action, using default.");
#ifdef DEBUG
	debug(__FUNCTION__, "Mrts label action is %d", MrtControls.labeAction);
#endif

	return err;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Save the  mrts control  settings to the config file.
 * Input:  none
 * Output:  retuns 0 on success, 1 on failure
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
saveMrtControlSettings()
{
	int err = 0;
	
	// save mrts tag
	if ( openConfigElement(XML_MRTS_CTR_TAG) )
	{
		err = 1;
		log_warning("Failed to save mrts tag to config file.");
	}

	// save listen addr
	if ( setConfigValueAsString(XML_MRTS_CTR_LISTEN_ADDR, MrtControls.listenAddr) ) 
	{
		err = 1;
		log_warning("Failed to save mrt listen address to config file.");
	}

	// save listen port
	if ( setConfigValueAsInt(XML_MRTS_CTR_LISTEN_PORT, MrtControls.listenPort) ) 
	{
		err = 1;
		log_warning("Failed to save mrt listen port to config file.");
	}

	// save the status of mrts control module
	if (setConfigValueAsInt(XML_MRTS_CTR_ENABLED, MrtControls.enabled) ) 
	{
		err = 1;
		log_warning("Failed to save mrt listen enabled status to config file.");
	}

	// save the max number of mrts
	if (setConfigValueAsInt(XML_MRTS_CTR_MAX_MRTS, MrtControls.maxMrts) ) 
	{
		err = 1;
		log_warning("Failed to save max mrts to config file.");
	}

	// save the label action of mrts
	if (setConfigValueAsInt(XML_MRTS_CTR_LABEL_ACTION, MrtControls.labeAction) ) 
	{
		err = 1;
		log_warning("Failed to save label action of mrts to config file.");
	}

	// save mrts tag
	if ( closeConfigElement() )
	{
		err = 1;
		log_warning("Failed to save mrts tag to config file.");
	}

	return err;
}

/*--------------------------------------------------------------------------------------
 * Purpose: launch mrt control thread, called by main.c
 * Input:  none
 * Output: none
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void 
launchMrtControlThread()
{
	int error;
	
	pthread_t mrtCtrThreadID;
#ifdef DEBUG
	debug(__FUNCTION__, "Creating Mrt Control thread...");
#endif
	if ((error = pthread_create(&mrtCtrThreadID, NULL, mrtControlThread, NULL)) > 0 )
		log_fatal("Failed to create Mrt Control thread: %s\n", strerror(error));

	MrtControls.mrtListenerThread = mrtCtrThreadID;
#ifdef DEBUG
	debug(__FUNCTION__, "Created Mrt Control thread!");
#endif
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the state of mrt control
 * Input:
 * Output: returns TRUE or FALSE
 *
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
isMrtControlEnabled()
{
	return MrtControls.enabled;
}

/*--------------------------------------------------------------------------------------
 * Purpose: enable the mrt control
 * Input:	
 * Output: 
 *
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
void 
enableMrtControl()
{
	MrtControls.enabled = TRUE;
}

/*--------------------------------------------------------------------------------------
 * Purpose: disable the mrt control
 * Input:	
 * Output: 
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
void 
disableMrtControl()
{
	MrtControls.enabled = FALSE;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the listening port of mrt control
 * Input:	
 * Output: the listening port of mrt control module 
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
getMrtControlListenPort()
{
	return MrtControls.listenPort;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Set the listening port of mrt control
 * Input:	the port
 * Output: 
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
void 
setMrtControlListenPort( int port )
{
	if( MrtControls.listenPort != port && port > 0)
	{
		MrtControls.rebindFlag = TRUE;
		MrtControls.listenPort = port;
	}
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the listening address of mrt control
 * Input:	
 * Output: the listening address for mrt control
 *          or NULL if no address for mrt control
 * Note: 1. The caller doesn't need to allocate memory.
 *       2. The caller must free the string after using it.
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
char * 
getMrtControlListenAddr()
{
        // allocate memory for the result
        char *ans = malloc(sizeof(MrtControls.listenAddr));
        if (ans == NULL) {
                log_err("getMrtControlListenAddr: couldn't allocate string memory");
                return NULL;
        }
        // copy the string and return result
        //strncpy(ans, MrtControls.listenAddr, length);
	memcpy(ans, MrtControls.listenAddr, sizeof(MrtControls.listenAddr));
        return ans;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Set the listening address of mrt control
 * Input:	the addr in string format
 * Output: ADDR_VALID means address is valid and set successfully.
 *	   ADDR_ERR_INVALID_FORMAT means this is a invalid-format address
 *	   ADDR_ERR_INVALID_PASSIVE means this is not a valid passive address
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
setMrtControlListenAddr( char *addr )
{
	int result = checkAddress(addr, ADDR_PASSIVE);
	if(result == ADDR_VALID)
	{
		if( strcmp(MrtControls.listenAddr, addr) != 0 )
		{
			MrtControls.rebindFlag = TRUE;
			strncpy(MrtControls.listenAddr, addr, ADDR_MAX_CHARS);
		}
	}
	return result;

}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the last action time for this thread
 * Input:	
 * Output: a timevalue indicating the last time the thread was active
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
time_t 
getMrtControlLastAction()
{
	return MrtControls.lastAction;
}

/*--------------------------------------------------------------------------------------
 * Purpose: the main function of mrt control module
 *  listens for mrt connections and starts new thread for each mrt connection
 * Input:  none
 * Output: none
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void *
mrtControlThread( void *arg )
{

	fd_set read_fds; 	// file descriptor list for select()
	int fdmax = 0;		// maximum file descriptor number
	int listenSocket = -1;	// socket to listen for connections

	// timer to periodically check thread status
	struct timeval timeout; 
	timeout.tv_usec = 0;
	timeout.tv_sec = THREAD_CHECK_INTERVAL; 

	log_msg( "Mrt control thread started." );
	
	// listen for connections and start new threads as needed.
  	while ( MrtControls.shutdown == FALSE ) 
	{
		// update the last active time for this thread
		MrtControls.lastAction = time(NULL);

		// check if mrt control is disabled
		if( MrtControls.enabled == FALSE )
		{
			// close the listening socket if active
			if( listenSocket >= 0 )
			{
#ifdef DEBUG
				debug( __FUNCTION__, "Close the listening socket(%d)!! ", listenSocket );
#endif
				close( listenSocket );
				FD_ZERO( &read_fds );
				fdmax = 0;
				listenSocket = -1;			
			}
#ifdef DEBUG
			debug( __FUNCTION__, "mrt control thread is disabled");
#endif	
		}
		// if it is enabled
		else
		{
			// if addr/port changed, close the old socket
			if( (listenSocket != -1) && (MrtControls.rebindFlag == TRUE) )
			{
				close( listenSocket );
				FD_ZERO( &read_fds );
				fdmax = 0;
				listenSocket = -1;			
				MrtControls.rebindFlag = FALSE;
#ifdef DEBUG
				debug( __FUNCTION__, "Close the listening socket(%d)!! ", listenSocket );
#endif
			}
			// if socket is down, reopen
			if (listenSocket == - 1) 
			{
				listenSocket = startMrtListener();
				// if listen succeeded, setup FD values
				// otherwise we will try next loop time
				if (listenSocket != - 1) 
				{
					FD_ZERO( &read_fds );
					FD_SET(listenSocket, &read_fds);
					fdmax = listenSocket+1;
#ifdef DEBUG
					debug( __FUNCTION__, "Opened the listening socket(%d)!! ", listenSocket );
#endif
				}
			}
			else
			{
				FD_ZERO( &read_fds );
				FD_SET(listenSocket, &read_fds);
				fdmax = listenSocket+1;
			}
#ifdef DEBUG
			debug( __FUNCTION__, "mrt control thread is enabled" );
#endif
		}

		if( select(fdmax, &read_fds, NULL, NULL, &timeout) == -1 )
		{
			log_err("mrt control thread select error:%s", strerror(errno));
			continue;
		}
		timeout.tv_usec = 0;
		timeout.tv_sec = THREAD_CHECK_INTERVAL; 


		if( listenSocket >= 0)
		{
			if( FD_ISSET(listenSocket, &read_fds) )//new mrt connection
			{
#ifdef DEBUG
				debug( __FUNCTION__, "new mrt attempting to start." );
#endif
				startMrt( listenSocket );
			}
		}
	}
	
	// close socket if open
	if( listenSocket != -1) 
	{
		close(listenSocket);
		FD_ZERO( &read_fds );
		fdmax = 0;
		listenSocket = -1;			
#ifdef DEBUG
		debug( __FUNCTION__, "Close the listening socket(%d)!! ", listenSocket );
#endif
	}
	log_warning( "MRT control thread exiting" );	 
	return NULL;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Start to mrt listen on the configured addr+port.
 * Input:  none
 * Output: socket ID of the listener or -1 if listener create fails
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
startMrtListener()
{	 
	// socket to listen for incoming connections
	int listenSocket = 0;

	// create addrinfo struct for the listener
	struct addrinfo *res = createAddrInfo(MrtControls.listenAddr, MrtControls.listenPort);
	if( res == NULL ) 
	{
		log_err( "mrt control thread createAddrInfo error!" );
		listenSocket = -1;
		return listenSocket;
	}

	// open the listen socket
	listenSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if ( listenSocket == -1 )
	{
		log_err( "fail to create mrt listener socket %s", strerror(errno) );
		freeaddrinfo(res);
		return listenSocket;
	}	

#ifdef DEBUG
	// allow rapid reuse of socket if in debug mode
	int yes=1;
	if(setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,&yes, sizeof(int)) < 0)
		log_err("setsockopt error");
#endif

	// bind to configured address and port
	if (bind(listenSocket, res->ai_addr, res->ai_addrlen) < 0)
	{
		log_err( "mrt listener unable to bind %s", strerror(errno) );
		close(listenSocket);
		freeaddrinfo(res);
		listenSocket = -1;
		return listenSocket;
	}	

	//start listening
	if (listen(listenSocket, 0) < 0) {
		log_err( "mrt listener unable to listen" );
		freeaddrinfo(res);
		close(listenSocket);
		listenSocket = -1;
		return listenSocket;
	}
	freeaddrinfo(res);

	return(listenSocket);	
}

/*--------------------------------------------------------------------------------------
 * Purpose: Accept the new mrt connection and spawn a new thread for it 
 *          if more mrts are allowed and it passes the ACL check.
 * Input:  the socket used for listening
 * Output: none
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void 
startMrt( int listenSocket )
{
	// structure to store the mrt's addrress from accept
	struct sockaddr_storage mrtaddr;
	socklen_t addrlen = sizeof (mrtaddr);
	memset(&mrtaddr,0,sizeof(struct sockaddr_storage));
	
	// accept connection
	int mrtSocket = accept( listenSocket, (struct sockaddr *) &mrtaddr, &addrlen);
	if( mrtSocket == -1 ) {
		log_err( "Failed to accept new mrt connection" );
		return;
	}

	// convert address into address string and port
	char *addr;
	int port;
	if( getAddressFromSockAddr((struct sockaddr *)&mrtaddr, &addr, &port) )
	{
		log_warning( "Unable to get address and port for new mrt connection." );
		return;
	}
#ifdef DEBUG 	
	else
		debug(__FUNCTION__, "mrt connection request from: %s, port: %d ", addr, port);
#endif

	// too many mrt connetions, close this one
	if ( MrtControls.activeMrts == MrtControls.maxMrts )
	{
		log_warning( "At maximum number of connected mrts: connection from %s port %d rejected.", addr, port );
		close(mrtSocket);
		free(addr);
		return;
	}

	//check the new mrt connection against ACL
	int labelAction = -1;
	if ( (labelAction=checkACL((struct sockaddr *) &mrtaddr, MRT_ACL))==0 )
	{
		log_msg("mrt connection from %s port %d rejected by access control list",addr, port);
		close(mrtSocket);
		free(addr);
		return;
	}
	
	// create a mrt node structure
	MrtNode *cn = createMrtNode(MrtControls.nextMrtID,addr, port, mrtSocket, labelAction);
	if (cn == NULL) {
		log_warning( "Failed to create mrt node structure.   Closing connection from  %s port %d rejected.", addr, port );
		close(mrtSocket);
		free(addr);
		return;
	}
	
	// lock the mrt list
	if ( pthread_mutex_lock( &(MrtControls.mrtLock) ) )
		log_fatal( "lock mrt list failed" );

	// add the mrt to the list 
	cn->next = MrtControls.firstNode;
	MrtControls.firstNode = cn;
	
	//increment the number of active mrts
	MrtControls.activeMrts++;
	MrtControls.nextMrtID++;


	
	// unlock the mrt list
	if ( pthread_mutex_unlock( &(MrtControls.mrtLock) ) )
		log_fatal( "unlock mrt list failed");

	// spawn a new thread for this mrt connection
	pthread_t mrtThreadID;
	int error;
	if ((error = pthread_create( &mrtThreadID, NULL, &mrtThread, cn)) > 0) {
		log_warning("Failed to create mrt thread: %s", strerror(error));
		destroyMrt(cn->id);
	}  

	cn->mrtThreadID = mrtThreadID;


#ifdef DEBUG
	debug(__FUNCTION__, "mrt connection accepted from: %s, port: %d ", addr, port);
#endif
	free(addr);
	return;
}

/*--------------------------------------------------------------------------------------
 * Purpose: print all the active mrts
 * Input:  none
 * Output: none
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void 
printMrts()
{
	// print all connected mrts for debugging
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		long id = cn->id;
		char *addr = getMrtAddress(id);
		int port = getMrtPort(id);
		long write = getMrtWriteItems(id);
		time_t connectedTime = getMrtConnectedTime(id);
		log_msg("Mrt id: %ld  addr: %s, port: %d, write:%ld, connectedTime:%d", id, addr, port, write, connectedTime);			
		free(addr);
		cn = cn->next;
	}	
}

/*--------------------------------------------------------------------------------------
 * Purpose: Intialize the shutdown process for the mrt module
 * Input:  none
 * Output: none
 * Kevin Burnett @ July 10, 2009
 * -------------------------------------------------------------------------------------*/
void signalMrtShutdown() 
{
	MrtControls.shutdown = TRUE;
}

/*--------------------------------------------------------------------------------------
 * Purpose: wait on all mrt pieces to finish closing before returning
 * Input:  none
 * Output: none
 * Kevin Burnett @ July 10, 2009
 * -------------------------------------------------------------------------------------*/
void waitForMrtShutdown()
{
	void * status = NULL;

	// wait for client listener control thread to exit
	pthread_join(MrtControls.mrtListenerThread, status);

	// wait for each update client connection thread to exit
	MrtNode * qn = MrtControls.firstNode;
	while(qn!=NULL) {
		status = NULL;
		deleteMrt(qn->id);
		//pthread_join(qn->mrtThreadID, status);
		//free(qn);
		qn = qn->next;
	}
}




