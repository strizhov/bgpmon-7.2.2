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
 *	OTHER DEALINGS IN THE SOFTWARE.
 * 
 * 
 *  File: mrtinstance.h
 * 	Authors: He Yan, Dan Massey
 *  Date: Oct 7, 2008 
 */

#ifndef MRTINSTANCE_H_
#define MRTINSTANCE_H_

#include <pthread.h>

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


#include <stdio.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <netinet/in.h>

#define TABLE_TRANSFER_SLEEP 30

// needed for ADDR_MAX_CHARS
#include "../Util/bgpmon_defaults.h"
#include "../Util/bgpmon_formats.h"
#include "../Util/bgp.h"

// needed for QueueWriter
#include "../Queues/queue.h"

#define UNKASNLEN -1
// 4096 + 12
#define MAX_MRT_LENGTH  4108
// BGP header (19)  MESSAGE header (16)
#define MRT_MRT_MSG_MIN_LENGTH  35 
#define MRT_HEADER_LENGTH 12
#define MRT_READ_2BYTES(a,b) a=ntohs(*((uint16_t*)&b))
#define MRT_READ_4BYTES(a,b) a=ntohl(*((uint32_t*)&b))

struct MRTStruct
{
        u_int32_t               timestamp;
        u_int16_t               type;
        u_int16_t               subtype;
        u_int32_t               length;
} ;
typedef struct MRTStruct MRTheader;

struct MRTMessageStruct
{
  uint32_t peerAs;
  uint32_t localAs;
  uint16_t interfaceIndex;
  uint16_t addressFamily;
  uint8_t  peerIPAddress[16];
  uint8_t  localIPAddress[16];
  char  peerIPAddressString[ADDR_MAX_CHARS];
  char  localIPAddressString[ADDR_MAX_CHARS];
};
typedef struct MRTMessageStruct MRTmessage;

/* structure holding mrt information  */
struct MrtStruct
{
	int		id;			// mrt ID number
	char		addr[ADDR_MAX_CHARS];	// mrt's address
	int		port;			// mrt's port
	int		socket;			// mrt's socket for reading
	time_t		connectedTime;		// mrt's connected time
	time_t		lastAction;		// mrt's last action time
	QueueWriter 	qWriter;		// mrt's Peer queue writer 
	int		deleteMrt;		// flag to indicate delete
	int 		labelAction;		// default label action for
	struct MrtStruct *	next;		// pointer to next mrt node
	pthread_t	mrtThreadID;		// thread reference
};
typedef struct MrtStruct MrtNode;


// MRT Peer_Index_Table,   subtype = 1
struct MRT_Index_Table
{
	u_int32_t BGPSrcID;
	u_int16_t ViewNameLen;
	u_int16_t ViewName;
	u_int16_t PeerCount;
	u_int8_t  PeerType;
	u_int32_t PeerBGPID;
	u_int8_t  PeerIP[16];
	char  PeerIPStr[ADDR_MAX_CHARS];
	u_int32_t PeerAS;
};
typedef struct MRT_Index_Table mrt_index;

// MRT RIB IP
struct MRT_RIB_Table
{
	u_int32_t SeqNumb;
	u_int8_t PrefixLen;
	char    Prefix[16];
	u_int16_t EntryCount;
	u_int16_t PeerIndex;
	u_int32_t OrigTime;
	u_int16_t AttrLen;
	u_char    BGPAttr[4096];
}; 
typedef struct MRT_RIB_Table mrt_uni;

// linked list of BGP messages
struct BGPTableStruct 
{
	// includes WDLen=0, Attr Len, Attr, NLRI(Pref len, Pref), Could be MP_REACH
	BGPMessage *BGPmessage;
	// length of entire BGP message
	int length;
	// link to next structure
	struct BGPTableStruct *next;
} ;

struct BufferStructure
{
	// Session ID
	int ID;
	// flag which shows existence of linked list of BGP messages
	int table_exist;
	// linked list
	struct BGPTableStruct *start;
   	struct BGPTableStruct *tail;
};
typedef struct BufferStructure  TableBuffer;


/*-------------------------------------------------------------------------------------- 
 * Purpose: Create a new MrtNode structure.
 * Input:  the mrt ID, address (as string), port, and socket
 * Output: a pointer to the new MrtNode structure 
 *         or NULL if an error occurred.
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
MrtNode * createMrtNode( long ID, char *addr, int port, int socket, int labelAction );

/*--------------------------------------------------------------------------------------
 * Purpose: The main function of a thread handling one mrt
 * Input:  the mrt node structure
 * Output: none
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void * mrtThread( void *arg );

/*--------------------------------------------------------------------------------------
 * Purpose: insert bmf messages into buffer
 * Input:  peers pointer to linked structure, tail of structure(makes addition faster), BMF message
 * Output: return linked list for every node, also updates tail by address
 * Mikhail Strizhov @ Oct 5, 2010
 * -------------------------------------------------------------------------------------*/
int insertBGPTable(TableBuffer *tbl, BGPMessage *bgp );

/*--------------------------------------------------------------------------------------
 * Purpose: free bmf messages into buffer
 * Input:  peers pointer to linked structure
 * Output: 
 * Mikhail Strizhov @ Oct 5, 2010
 * -------------------------------------------------------------------------------------*/
void freeBGPTable ( TableBuffer *tbl) ;

/*--------------------------------------------------------------------------------------
 * Purpose: write bmf messages to peer queue
 * Input:  peers pointer to linked structure, queue pointer
 * Output: 
 * Mikhail Strizhov @ Oct 6, 2010
 * -------------------------------------------------------------------------------------*/
void writeBGPTableToQueue(int ID, struct BGPTableStruct **start, QueueWriter writerpointer, int asLen );

/*--------------------------------------------------------------------------------------
 * Purpose: check how many null and non null tables we have 
 * Input:  tablebuffer pointer,  number of peers in table
 * Output:  0 failure, 1 success (signal to exit while loop)
 * Mikhail Strizhov @ Oct 6, 2010
 * -------------------------------------------------------------------------------------*/
int checkBGPTableEmpty(TableBuffer  *tablebuffer, int NumberPeers);

/*--------------------------------------------------------------------------------------
 * Purpose: free bmf messages into buffer
 * Input:  peers pointer to linked structure
 * Output: 
 * Mikhail Strizhov @ Oct 5, 2010
 * -------------------------------------------------------------------------------------*/
u_char * changeMRTASpath( u_char *temp, int pathlength );

/*--------------------------------------------------------------------------------------
 * Purpose: Get the connected mrt's address
 * Input: ID of the mrt
 * Output: address of this mrt in a char array. 
 *         or NULL if there is no mrt with this ID 
 * Note: 1. The caller doesn't need to allocate memory.
 *       2. The caller must free the string after using it.
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
char * 
getMrtAddress(long ID);

/*--------------------------------------------------------------------------------------
 * Purpose: Get the connected mrt's port
 * Input: ID of the mrt
 * Output: port of this mrt 
 *         or -1 if there is no mrt with this ID 
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
getMrtPort(long ID);

/*--------------------------------------------------------------------------------------
 * Purpose: Get the connected time of a mrt
 * Input: ID of the mrt
 * Output: connected time of this mrt
 * 		  or time = 0 if there is no mrt with this ID
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
time_t 
getMrtConnectedTime(long ID);

/*--------------------------------------------------------------------------------------
 * Purpose: Get an array of IDs of all active mrt connections
 * Input: a pointer to an unallocated array
 * Output: the length of the array or -1 on failure
 * Note: 1. The caller doesn't need to allocate memory.
 *       2. The caller must free the array after using it.
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
getActiveMrtsIDs(long **mrtIDs);

int submitBMF(MrtNode *cn, MRTheader *mrtHeader, MRTmessage *mrtMessage, BMF bmf );
void MRT_handleMessage(MrtNode *cn, MRTheader *mrtHeader,MRTheader *mrtHeader_p, MRTmessage *mrtMessage, MRTmessage *mrtMessage_p, uint8_t *raw_message, int length);

//functions that have unit testing
int  MRT_createTableBufferFromType13Subtype1(MrtNode*,mrt_index*,TableBuffer**,const uint8_t*,MRTheader*);
int  MRT_processType13SubtypeSpecific(MRTheader *mrtHeader,const uint8_t *rawMessage, BGPMessage*** bmf_arr,int **peer_idxs,int *bmf_count);
int  MRT_processType16SubtypeMessage(uint8_t *rawMessage,int asNumLen,MRTheader *mrtHeader,MRTmessage *mrtMessage,BMF *bmf);

// functions that do not have unit testing
int  MRT_readMessage(int socket,MRTheader *mrtHeader,uint8_t *rawMessage);
int  MRT_parseHeader(int socket,MRTheader* mrtHeader);
int  MRT_processType13SubtypeGeneric(MRTheader *mrtHeader,const uint8_t *rawMessage, BGPMessage ***bgp_arr,int **peer_idxs,int *bmf_count);
int  MRT_processType13(MrtNode *cn, uint8_t *rawMessage, MRTheader *mrtHeader);
int  fastForward(int socket, int length);
int  fastForwardToBGPHeader(int socket);


#endif /*MRTINSTANCE_H_*/
