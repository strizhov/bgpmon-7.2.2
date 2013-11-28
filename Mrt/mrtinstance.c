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
 *  File: mrtinstance.c
 * 	Authors: He Yan, Dan Massey
 *
 *  Date: Oct 7, 2008 
 */
 
/* 
 * Maintain a connection to a mrt box
 */

/* externally visible structures and functions for mrts */
#include "mrt.h"
/* internal structures of Mrt Control module */
#include "mrtcontrol.h"
/* internal structures and functions for this module */
#include "mrtinstance.h"

/* required for logging functions */
#include "../Util/log.h"
#include "../Util/utils.h"

/* needed for address management  */
#include "../Util/address.h"

/* needed for writen function  */
#include "../Util/unp.h"
#include "../Util/bgp.h"

/* needed for function getXMLMessageLen */
#include "../XML/xml.h"

/* needed for session related function */
#include "../Peering/peersession.h"

/* needed for Mrt Table Dump v2 and state of connection */
#include "../Peering/bgppacket.h"
#include "../Peering/bgpmessagetypes.h"

/* needed for deleteRib table if socket connection lost */
#include "../Labeling/label.h"

/* needed for session State */
#include "../Peering/bgpstates.h"
#include "../Peering/bgpevents.h"


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

//#define DEBUG


/*--------------------------------------------------------------------------------------
 * Purpose: Get an array of IDs of all active mrt connections
 * Input: a pointer to an unallocated array
 * Output: the length of the array or -1 on failure
 * Note: 1. The caller doesn't need to allocate memory.
 *       2. The caller must free the array after using it.
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
getActiveMrtsIDs(long **mrtIDs)
{
	// lock the mrt list
	if ( pthread_mutex_lock( &(MrtControls.mrtLock) ) )
		log_fatal("lock mrt list failed");


	// allocate an array whose size depends on the active mrt
	long *IDs = malloc(sizeof(long)*MrtControls.activeMrts);
	if (IDs == NULL) {
		log_err("Failed to allocate memory for getActiveMrtsIDs");
		*mrtIDs = NULL; 
		return -1;
	}


	// for each active mrt, add its ID to the array
	MrtNode *cn = MrtControls.firstNode;
	int i = 0;
	while( cn != NULL )
	{
		IDs[i] = cn->id;
		i++;
		cn = cn->next;
	}
	*mrtIDs = IDs; 

	//sanity check how many IDs we found
	if (i != MrtControls.activeMrts)
	{
		log_err("Unable to get Active Mrt IDs!");
		free(IDs);
		*mrtIDs = NULL; 
		i = -1;
	}

	// unlock the mrt list
	if ( pthread_mutex_unlock( &(MrtControls.mrtLock) ) )
		log_fatal( "unlock mrt list failed");

	return i;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the connected mrt's port
 * Input: ID of the mrt
 * Output: port of this mrt 
 *         or -1 if there is no mrt with this ID 
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
getMrtPort(long ID)
{
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		if(cn->id == ID)
			return cn->port;
		else	
			cn = cn->next;
	}

	log_err("getMrtPort: couldn't find a mrt connection with ID: %d", ID);
	return -1;
}

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
getMrtAddress(long ID)
{
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		if(cn->id == ID)
		{
			char *ans = malloc( sizeof(char)*(ADDR_MAX_CHARS+1) );
			if (ans == NULL) {
				log_err("getMrtAddress: couldn't allocate string memory");
				return NULL;
			}
			memset(ans,'\0', ADDR_MAX_CHARS);
			strncpy(ans, cn->addr, strlen(cn->addr) );
			ans[ADDR_MAX_CHARS] = '\0';
			return ans;
		}
		else	
			cn = cn->next;
	}

	log_err("getMrtAddress: couldn't find a mrt with ID: %d", ID);
	return NULL;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the connected time of a mrt
 * Input: ID of the mrt
 * Output: connected time of this mrt
 * 		  or time = 0 if there is no mrt with this ID
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
time_t 
getMrtConnectedTime(long ID)
{
	time_t ans = 0;
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		if(cn->id == ID)
			return cn->connectedTime;
		else	
			cn = cn->next;
	}

	log_err("getMrtConnectedTime: couldn't find a mrt with ID: %d", ID);
	return ans;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the labelAction of a mrt
 * Input: ID of the mrt
 * Output: label Action of this mrt
 * Mikhail Strizhov @ Jan 30, 2009
 * -------------------------------------------------------------------------------------*/ 
int 
getMrtLabelAction(long ID)
{
	int ans = 0;
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		if(cn->id == ID)
			return cn->labelAction;
		else	
			cn = cn->next;
	}

	log_err("getMrtConnectedTime: couldn't find a mrt with ID: %d", ID);
	return ans;
}


/*--------------------------------------------------------------------------------------
 * Purpose: Get the number of items written by this mrt connection
 * Input: ID of the mrt
 * Output: the number of items
 * 	   or -1 if there is no mrt with this ID
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------
 */ 
long
getMrtWriteItems(long ID)
{
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		if(cn->id == ID)
		{
		}
		else	
			cn = cn->next;
	}

	log_warning("getMrtWriteItems: couldn't find a mrt with ID:%d", ID);
	return -1;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Get the last action time for this mrt connection
 * Input: ID of the mrt
 * Output: a timevalue indicating the last time the thread was active
 *         or time = 0 if there is no mrt with this ID
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/
time_t
getMrtLastAction(long ID)
{
	time_t ans = 0;
	MrtNode *cn = MrtControls.firstNode;
	while( cn != NULL )
	{
		if(cn->id == ID)
			return  cn->lastAction;
		else	
			cn = cn->next;
	}

	log_err("getMrtLastAction: couldn't find a mrt with ID: %d", ID);
	return ans;
}

/*--------------------------------------------------------------------------------------
 * Purpose: kill a mrt connection, close the thread and free all associated memory 
 * Input: ID of the mrt
 * He Yan @ Sep 22, 2008
 * -------------------------------------------------------------------------------------*/ 
void 
deleteMrt(long ID)
{
	MrtNode *cn = MrtControls.firstNode;
	log_msg("deleteMrt Called!");
	while( cn != NULL )
	{
		if(cn->id == ID)
		{
			cn->deleteMrt = TRUE;
			return;
		}
		else	
			cn = cn->next;
	}
	log_err("deleteMrt: couldn't find a mrt with ID:%d", ID);
}

/*--------------------------------------------------------------------------------------
 * Purpose:  destroy the socket and memory associated with a mrt ID
 * Input:  the mrt ID to destroy
 * Output: none
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void 
destroyMrt(long id)
{
	// lock the mrt list
	if ( pthread_mutex_lock( &(MrtControls.mrtLock) ) )
		log_fatal( "lock mrt list failed");

	MrtNode *prev = NULL;
	MrtNode *cn = MrtControls.firstNode;

	while(cn != NULL) {
		if (cn->id == id ) 
		{
			// close this mrt connection
			log_msg("Deleting mrt id (%d)", cn->id);		
			// close the mrt socket
			close( cn->socket );
			// remove from the mrt list
			MrtControls.activeMrts--;
			if (prev == NULL) 
				MrtControls.firstNode = cn->next;
			else
				prev->next = cn->next;
			// clean up the memory
			destroyQueueWriter( cn->qWriter );
			free(cn);
                        cn = NULL;
			// unlock the mrt list
			if ( pthread_mutex_unlock( &(MrtControls.mrtLock) ) )
				log_fatal( "unlock mrt list failed");			
			return;
		}
		else
		{
			prev = cn;
			cn = cn->next;
		}
	}

	log_err("destroyMrt: couldn't find a mrt with ID:%d", id);
	// unlock the mrt list
	if ( pthread_mutex_unlock( &(MrtControls.mrtLock) ) )
		log_fatal( "unlock mrt list failed");
	return;
}

/*--------------------------------------------------------------------------------------
 * Purpose: Create a new MrtNode structure.
 * Input:  the mrt ID, address (as string), port, and socket
 * Output: a pointer to the new MrtNode structure 
 *         or NULL if an error occurred.
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
MrtNode * 
createMrtNode( long ID, char *addr, int port, int socket, int labelAction )
{
	// create a mrt node structure
	MrtNode *cn = malloc(sizeof(MrtNode));
	if (cn == NULL) {
		log_warning("Failed to allocate memory for new mrt.");
		return NULL;
	}
	cn->id = ID;
	memset(cn->addr, '\0', ADDR_MAX_CHARS );
	strncpy( cn->addr, addr, strlen(addr) );
	cn->port = port;
	cn->socket = socket;
	cn->connectedTime = time(NULL);
	cn->lastAction = time(NULL);
	cn->qWriter= createQueueWriter( peerQueue );
	cn->deleteMrt = FALSE;		
	cn->next = NULL;
	cn->labelAction = labelAction;
	return cn;
}



/*--------------------------------------------------------------------------------------
 * Purpose: check how many null and non null tables we have 
 * Input:  tablebuffer pointer,  number of peers in table
 * Output:  0 failure, 1 success (signal to exit while loop)
 * Mikhail Strizhov @ Oct 6, 2010
 * -------------------------------------------------------------------------------------*/
int checkBGPTableEmpty(TableBuffer  *tablebuffer, int NumberPeers)
{	
	int retval = 0;
	int i=0;
	int nulltable = 0;
	int nonnulltable = 0;
	for (i=0;i<NumberPeers;i++)
	{
		if (tablebuffer[i].start == NULL)
		{
			nulltable++;
		}
		else
		{
			nonnulltable++;	
		}
	}

	log_msg("mrtThread, BGP Table still maintains %d peer tables", nonnulltable);
	// check if numberof nulltables is eq to NumberPeers and nonnull is 0
	if ((nulltable == NumberPeers) && (nonnulltable == 0))
	{
		retval = 1;
	}

	return retval;
}

/*--------------------------------------------------------------------------------------
 * Purpose: insert bmf messages into buffer
 * Input:  peers pointer to linked structure, tail of structure(makes addition faster), BMF message
 * Output: return linked list for every node, also updates tail by address
 * Mikhail Strizhov @ Oct 5, 2010
 * -------------------------------------------------------------------------------------*/
int insertBGPTable(TableBuffer *tbl, BGPMessage *bgp )
{
	struct BGPTableStruct *temp;

        if(tbl == NULL){
          log_err("Unable to add to non-existant table\n");
          return -1;
        }
	if(tbl->start==NULL)
	{
		tbl->start=calloc(1,sizeof(struct BGPTableStruct));
                if(tbl->start == NULL){
                  log_err("Malloc Error\n");
                  return -1;
                }
		tbl->tail = tbl->start;
                temp = tbl->start;
	}
	else
	{
		// get tail
		temp = tbl->tail;
		temp->next = calloc(1,sizeof(struct BGPTableStruct));
		temp=temp->next;
		// update tail
		tbl->tail = temp;
	}
	
	temp->BGPmessage = bgp;
	temp->next  = NULL;
        return 0;

}

/*--------------------------------------------------------------------------------------
 * Purpose: write bmf messages to peer queue
 * Input: SessionID, linked list, Queue writer, convert flag (1 or 0)
 * Output: none
 * Mikhail Strizhov @ Oct 6, 2010
 * -------------------------------------------------------------------------------------*/
void writeBGPTableToQueue(int ID, struct BGPTableStruct **start, QueueWriter writerpointer, int asLen)
{
	// check if its already free
	if( *start == NULL ) return;   
	
	struct BGPTableStruct *ptr = NULL;
	for (ptr = *start; ptr != NULL; ptr = ptr->next) 
	{
          BMF m = createBMF(-1,BMF_TYPE_TABLE_TRANSFER);
          char *bgpSerialized =(char*)BGP_serialize(ptr->BGPmessage,asLen);
          int bgp_length;
          if(asLen == 2){
            bgp_length = BGP_calculateLength(ptr->BGPmessage,1);
          }else{
            bgp_length = BGP_calculateLength(ptr->BGPmessage,0);
          }
          bgpmonMessageAppend(m,bgpSerialized,bgp_length);
          free(bgpSerialized);
          bgpSerialized = NULL;
          BGP_freeMessage(ptr->BGPmessage);
	  writeQueue(writerpointer, m );
	  incrementSessionMsgCount(ID);
	} // for loop; end
}

/*--------------------------------------------------------------------------------------
 * Purpose: free linked list of BGP messages
 * Input:  peers pointer to linked structure
 * Output: none
 * Mikhail Strizhov @ Oct 5, 2010
 * -------------------------------------------------------------------------------------*/
void freeBGPTable ( TableBuffer *tbl) 
{
	struct BGPTableStruct *ptr1,*ptr2;

	ptr1=tbl->start;
        while( ptr1!=NULL ) 
	{
                ptr2=ptr1->next; 
                BGP_freeMessage(ptr1->BGPmessage);
		free(ptr1);
                ptr1 = ptr2;
	}
        tbl->start = tbl->tail = NULL;
}


/*--------------------------------------------------------------------------------------
 * Purpose: free bmf messages into buffer
 * Input:  peers pointer to linked structure
 * Output: 
 * Mikhail Strizhov @ Oct 5, 2010
 * -------------------------------------------------------------------------------------*/
u_char * changeMRTASpath( u_char *temp, int pathlength )
{
	int i=0;	
	int ASmover = 0;
	u_int16_t ASN=0;
	
	u_char *changedASSEQ=NULL;
	changedASSEQ = malloc(512*sizeof(u_char));
	if (changedASSEQ == NULL)
	{
		log_msg("Malloc failed!!");
		return NULL;
	}
	
	memset(changedASSEQ, '\0', 512);

	int counter=0;

	for (i=0;i<pathlength*2;i++)
	{
		ASN= ntohs(*((u_int16_t *) (&temp[ASmover])));
		if (ASN != 0)
		{
			memcpy(changedASSEQ+counter, &temp[ASmover], 2);
			counter = counter + 2;
		}
#ifdef DEBUG
		printf("ASN is %d\n", ASN);
#endif		
		ASmover = ASmover+2;
	}

#ifdef DEBUG
	printf("Modified AS seq is \n");
	ASmover=0;
	for (i=0; i<pathlength;i++)
	{
		ASN= ntohs(*((u_int16_t *) (&changedASSEQ[ASmover])));
		printf("NEW ASN is %d\n", ASN);
		ASmover = ASmover+2;
		
	}
#endif	

	return changedASSEQ;
}


/*--------------------------------------------------------------------------------------
 * Purpose: The main function of a thread handling one mrt connection
 * Input:  the mrt node structure for this mrt
 * Output: none
 * He Yan @ July 22, 2008
 * There are two types of MRT conversations. The first consists of a series of update messages
 * as of revision 15 of the rfc this will mainly consist of type 16 subtype messages.
 * The second is a table dump (type 13 subtypes 1-6)
 * Each mrt thread will handle only one of these conversations.
 * The update messages are the default case -- those are handled here in the main loop
 * The table dumps are detected and a separate function is called. 
 * When that function returns the main loop should exit.
 * -------------------------------------------------------------------------------------*/
void *
mrtThread( void *arg )
{
  // the mrt node structure
  MrtNode *cn = arg;	

  // the object that will be used repeatedly to hold mrt data
  MRTheader mrtHeader;
  MRTheader mrtHeader_p;
  MRTmessage mrtMessage;
  MRTmessage mrtMessage_p;
  BMF bmf;
  BMF bmf_p=NULL;
  uint8_t rawMessage[MAX_MRT_LENGTH];

  // eof is a flag that is set on a read error or on and actual EOF
  short eof = 0;

  // while the mrt connection is alive, read from it
  // convert it to BMF format and write it into peer queue
  // We can't write to the queue until we see that the next
  // MRT message is valid as well.
  while ( cn->deleteMrt==FALSE && !eof ){
    // update the last action time
    cn->lastAction = time(NULL);
    int res = MRT_readMessage(cn->socket,&mrtHeader,rawMessage);
    if(0 > res){
      eof = 1;
      continue;
    }else if(res > 0){
      // failure: zero out all current data 
      log_err("mrtThread: Unable to parse MRT message: removing previous as well\n");
      log_err("mrtThread: Previous (len=%lu type=%u subtype=%u)\n",mrtHeader_p.length,mrtHeader_p.type,mrtHeader_p.subtype);
      memset(&mrtMessage_p,0,sizeof(MRTmessage));
      memset(&mrtHeader_p,0,sizeof(MRTheader));
      if(bmf_p != NULL){
        destroyBMF(bmf_p);
        bmf_p = NULL;
      } 
    }

#ifdef DEBUG
  log_msg("mrtThread, time %lu type %u subtype %u length %lu\n",mrtHeader.timestamp,mrtHeader.type,mrtHeader.subtype, mrtHeader.length);
#endif

    // http://tools.ietf.org/html/draft-ietf-grow-mrt-17#section-4
    //    The following MRT Types are currently defined for the MRT format.
    //The MRT Types which contain the "_ET" suffix in their names identify
    //those types which use an Extended Timestamp MRT Header.  The subtype
    //and message fields in these types remain as defined for the MRT Types
    //of the same name without the "_ET" suffix.
    //
    switch( mrtHeader.type ){
      //11   OSPFv2
      case 11: 
      //12   TABLE_DUMP
      case 12:
      //17   BGP4MP_ET
      case 17: 
      //32   ISIS
      case 32: 
      //33   ISIS_ET
      case 33:
      //48   OSPFv3
      case 48:
      //49   OSPFv3_ET
      case 49:
        log_warning("mrtThread, received an unsupported MRT message\n");
        log_warning("mrtThread, time %d type %u, subtype %u and len %u! skipping....",
                    mrtHeader.timestamp, mrtHeader.type, mrtHeader.subtype, mrtHeader.length);
      //13   TABLE_DUMP_V2
      // this entire conversation is handled elsewhere,
      // when it returns, we just clean up the socket
      case 13:
        // MRT_processType13(cn,rawMessage,&mrtHeader);
        // shutdown connection and clean memory
        destroyMrt(cn->id);	
        // exit thread
        pthread_exit( (void *) 1 ); 
        return NULL;
  
      //16   BGP4MP
      // this conversation uses this loop as its main loop.
      // if processing a message is successful, we submit the previous message to the queue
      case 16:
      {
        // this is defaulting to a 4 byte as length (the case statement changes it to 2 for type 1)
        int asNumLen = 4;
        //       0    BGP4MP_STATE_CHANGE
        //       1    BGP4MP_MESSAGE
        //       4    BGP4MP_MESSAGE_AS4
        //       5    BGP4MP_STATE_CHANGE_AS4
        //       6    BGP4MP_MESSAGE_LOCAL
        //       7    BGP4MP_MESSAGE_AS4_LOCAL
        switch(mrtHeader.subtype)
        {
          case 0:
          case 5:
            log_warning("mrtThread, recieved an unsupported subtype: %d\n",mrtHeader.subtype);
            break;
          case 1:
            asNumLen = 2;
          case 4:
            if(!MRT_processType16SubtypeMessage(rawMessage,asNumLen, &mrtHeader,&mrtMessage, &bmf)){
              // sucess parsing the current message
              if(mrtHeader_p.length > 0){
#ifdef DEBUG
  log_msg("mrtThread, submitting time %lu type %u subtype %u length %lu\n",mrtHeader_p.timestamp,
                           mrtHeader_p.type,mrtHeader_p.subtype, mrtHeader_p.length);
#endif
                submitBMF(cn,&mrtHeader_p,&mrtMessage_p,bmf_p);
              }
              // make the current bmf the previous for the next iteration
              memmove(&mrtHeader_p,&mrtHeader,sizeof(MRTheader));
              memset(&mrtHeader,0,sizeof(MRTheader));
              memmove(&mrtMessage_p,&mrtMessage,sizeof(MRTmessage));
              memset(&mrtMessage,0,sizeof(MRTmessage));
              bmf_p = bmf;
              bmf = NULL;
            }else{
              // failure: zero out all current data 
              log_err("mrtThread: Unable to parse MRT message: removing previous as well\n");
              log_err("mrtThread: Previous (len=%lu type=%u subtype=%u)\n",mrtHeader_p.length,mrtHeader_p.type,mrtHeader_p.subtype);
              memset(&mrtMessage,0,sizeof(MRTmessage));
              memset(&mrtHeader,0,sizeof(MRTheader));
              memset(&mrtMessage_p,0,sizeof(MRTmessage));
              memset(&mrtHeader_p,0,sizeof(MRTheader));
              if(bmf_p != NULL){
                destroyBMF(bmf_p);
                bmf_p = NULL;
              } 
              if(bmf != NULL){
                destroyBMF(bmf);
                bmf_p = NULL;
              } 
              if(fastForwardToBGPHeader(cn->socket)){
                eof = 1;
              }
            }
            break;
          case 6:
          case 7:
            log_warning("mrtThread, recieved an unsupported subtype\n");
            break;
          default:
            log_warning("mrtThread, received a corrupt MRT message with type %u, subtype %u and len %u!",
                         mrtHeader.type, mrtHeader.subtype, mrtHeader.length);
            if(bmf_p != NULL){
              log_warning("mrtThread, previous message with type %u subtype %u, len %u",
                         mrtHeader_p.type, mrtHeader_p.subtype,mrtHeader_p.length);
            }else{
              log_warning("mrtThread, previous message is not available\n");
            }
            if(fastForwardToBGPHeader(cn->socket)){
              eof = 1;
            }
            memset(&mrtHeader_p,0,sizeof(MRTheader));
            memset(&mrtMessage_p,0,sizeof(MRTheader));
            destroyBMF(bmf_p);
            bmf_p = NULL;
        }
        break;
      }
  
      // in this case we are likely looking at a corrupt message
      // we need to make sure that the previous message does not 
      // get processed as it may be the message that is corrupt.
      default:
        log_warning("mrtThread, received a corrupt MRT message with time %d type %u, subtype %u and len %u!",
                   mrtHeader.timestamp, mrtHeader.type, mrtHeader.subtype, mrtHeader.length);
        if(bmf_p != NULL){
          log_warning("mrtThread, previous message with type %u subtype %u, len %u",
                     mrtHeader_p.type, mrtHeader_p.subtype,mrtHeader_p.length);
        }else{
          log_warning("mrtThread, previous message is not available\n");
        }
        if(fastForwardToBGPHeader(cn->socket)){
          eof = 1;
        }
        memset(&mrtHeader_p,0,sizeof(MRTheader));
        memset(&mrtMessage_p,0,sizeof(MRTheader));
        destroyBMF(bmf_p);
        bmf_p = NULL;
    }
  }
  // the loop is has exited... this means that the connection has be shut down -- perform cleanup
  log_warning("MRT Node %d disconnected!", cn->id);

  //when we exit the loop, there may be one remaining message to push onto the queue
  log_warning("Final MRT submission");
  if(mrtHeader_p.length > 0){
    submitBMF(cn,&mrtHeader_p,&mrtMessage_p,bmf_p);
  }
  // change all sessions which were create by this mrt node to stateError
  log_warning("Deleting session");
  deleteMrtSession(cn->id  );
  // shutdown connection and clean memory
  log_warning("Destroying MRT");
  destroyMrt(cn->id);	
  // exit thread
  log_warning("Exiting Thread");
  return NULL;
}
/******************************************************************************
 * MRT_readMessage
 * Input: socket, pointer to the MRT header to populate, pointer to space
 *        for the raw message
 * Return: -1 on read error
 *          n on success; n= the number of messages fast forwarded over
 *                        to get to the next valid header
 *          0 on total sucess > 0 with some failures, but can proceed
 *****************************************************************************/
int
MRT_readMessage(int socket,MRTheader *mrtHeader,uint8_t *rawMessage){

  int forward = 0;

  while(MRT_parseHeader(socket,mrtHeader)){
    log_warning("mrtThread, header parsing failed\n");
    if(fastForwardToBGPHeader(socket)){
      return -1;
    }
    forward++;
  }
  if(forward > 0){
    log_err("mrtThread (readMessage): had to fast forward %d times to recover, new length is %lu\n",forward,mrtHeader->length);
  }
  // the MRT_parseHeader function will only populate a valid header
  // if we have reached this point the header looks valid, that really
  // only means that the length is not larger than our max
  int read_len = readn(socket,rawMessage,mrtHeader->length);
  if(read_len != mrtHeader->length){
    log_err("mrtThread, message reading failed\n");
    return -1;
  }
  return forward;
}
/******************************************************************************
 * MRT_handleMessage
 * input:
 * return: void
******************************************************************************/
/*void
MRT_handleMessage(MrtNode *cn,MRTheader *mrtHeader,MRTheader *mrtHeader_p,
                  MRTmessage *mrtMessage, MRTmessage *mrtMessage_p,
                  uint8_t *rawMessage, int length){

  }
}*/


/*--------------------------------------------------------------------------------------
 * Purpose: read in the mrt header
 * Input:  the socket to read from, a pointer the MRT header struct to populate
 * Output: 1 for success
 *         0 for failure --> should result in closing the MRT session
 * Cathie Olschanowsky @ 8/2011
 * first read the MRT common header http://tools.ietf.org/search/draft-ietf-grow-mrt-15#section-2
 * expected format
 *0                   1                   2                   3
 *0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *|                           Timestamp                           |
 *+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *|             Type              |            Subtype            |
 *+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *|                             Length                            |
 *+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *|                      Message... (variable)
 *+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
--------------------------------------------------------------------------------------*/
int 
MRT_parseHeader(int socket,MRTheader* mrtHeader)
{
  int n = readn( socket, mrtHeader, sizeof(MRTheader));
  if( n != sizeof(MRTheader) )
  {
    log_err("mrtThread, read MRT header: EOF may have been reached: attempted to read %u bytes and read %d",sizeof(MRTheader),n);
    return -1;
  }
  mrtHeader->timestamp = ntohl(mrtHeader->timestamp);
  mrtHeader->type = ntohs(mrtHeader->type);
  mrtHeader->subtype = ntohs(mrtHeader->subtype);
  mrtHeader->length = ntohl(mrtHeader->length);

  if(mrtHeader->length > MAX_MRT_LENGTH){
    log_err("mrtThread, read MRT header: invalid length %lu > %lu\n",mrtHeader->length,MAX_MRT_LENGTH);
    return -1;
  }
  if(mrtHeader->length < MRT_MRT_MSG_MIN_LENGTH){
    log_err("mrtThread, read MRT header: invalid length %lu < %lu\n",mrtHeader->length,MRT_MRT_MSG_MIN_LENGTH);
    return -1;
  }

  return 0;
}

/*--------------------------------------------------------------------------------------
 * Purpose: fast forward reading from the socket, the given number of bytes
 * Input:  the socket to read from, the number of bytes to skip
 * Output: 1 for success
 *         0 for failure
--------------------------------------------------------------------------------------*/
int 
fastForward(int socket, int length)
{

  int n = 0;
  u_int8_t *skip = malloc(length);
  if(skip == NULL)
  {
    log_err("mrtThread, unable to malloc %u for skipping\n",length);
    return -1;
  }else{
    n = readn( socket, skip, length );
    if( n != length )
    {
      log_err("mrtThread, unable to read skipped message");
      free(skip);
      return -1;
    }
  }
  free(skip);
  return 0;
}
/*--------------------------------------------------------------------------------------
 * Purpose: fast forward reading from the socket to the next valid BGP header
 * Input:  the socket to read from
 * Output: 1 for success
 *         0 for failure
 * we are searching for 16 255s in a row here.
 * the magic 18 is because 16 bytes + 2 bytes for length = 18 bytes
 * the lenght is inclusive in the BGP header
--------------------------------------------------------------------------------------*/
int 
fastForwardToBGPHeader(int socket)
{

  u_int8_t nextByte = 0;
  int i,n;

  while(1)
  {
    n = readn( socket, &nextByte, 1);
    if(n != 1){
      log_err("mrtThread, unable to read from socket\n");
      return -1;
    }
    if(nextByte == 255)
    {	
      for(i=0; i<15; i++)
      {
        n = readn( socket, &nextByte, 1);
        if(n != 1){
          log_err("mrtThread, unable to read from socket\n");
          return -1;
        }
        if( nextByte != 255 )
        {
          break;
        }
      }
      if( i == 15 )
      {
        u_int16_t len;
        u_int8_t *tmpBuf = NULL;
        n = readn( socket, &len, 2);
        if(n != 2){
          log_err("mrtThread, unable to read from socket\n");
          return -1;
        }
        len = ntohs(len);
        tmpBuf = malloc(len-18);
        if(tmpBuf == NULL){ 
          log_err("mrtThread, malloc error\n");
          return -1;
        }
        n = readn( socket, tmpBuf, len-18);
        if(n != (len-18)){
          log_err("mrtThread, unable to read from socket\n");
          free(tmpBuf);
          return -1;
        }
        log_msg("mrtThread, found next BGP message with len %d", len);
        free(tmpBuf);
        break;
      }
    }
  }
  #ifdef DEBUG
  //hexdump(LOG_WARNING, prebmf->message, prebmf->length);
  #endif
  return 0;
}

/*--------------------------------------------------------------------------------------
 * Purpose: this code processes MRT messages of type 13   TABLE_DUMP_V2
 * Input:  the socket to read from, the mrtheader object that put us here,
 * Output: 1 for success
 *         0 for failure
 * We are making the assumption that messages of type 13 will not be found in 
 * the same conversation as other types. Once we step into this subroutine
 * we will only process type 13s until disconnect.
 * TODO: this function needs more refactoring --> it is too long The Jira Issue is 
 * BGPMON-29
--------------------------------------------------------------------------------------*/
int 
MRT_processType13(MrtNode *cn, uint8_t *rawMessage, MRTheader *mrtHeader)
{

  mrt_index indexPtr; 	
  short eof = 0;
  TableBuffer *tablebuffer;
  BGPMessage **bgp_arr;
  int *peer_idxs;
  int *peer_idxs_prev = NULL;
  int bgp_count = 0;
  BGPMessage **bgp_prev = NULL;
  int bgp_count_prev = 0;
  int i;

  if(mrtHeader->subtype != 1){
    log_err("mrtThread, TABLE_DUMP_V2 initiated with subtype %d rather than 1\n",mrtHeader->subtype);
    return -1;
  }

  // the first message is used to create a temporary table of sessions.
  // if this fails we should drop the connection -- for now just return -1
  if(MRT_createTableBufferFromType13Subtype1(cn,&indexPtr,&tablebuffer,rawMessage,mrtHeader)){
    log_err("mrtThread, TABLE_DUMP_V2 the first message was not processed\n");
    return -1;  
  }

  // The Message 13 subtype 1 message is complete and now we recieve other messages
  // at this point we are in a conversation with the MRT collector and are 
  // expecting a series of messages of type 13 --> no other type
  // should be seen and we should not see any more of subtype 1.
#ifdef DEBUG
int debug_count = 0;
#endif
  while ( cn->deleteMrt==FALSE && !eof )
  {
#ifdef DEBUG
debug_count++;
#endif
    // update the last action time
    cn->lastAction = time(NULL);
    int res = MRT_readMessage(cn->socket,mrtHeader,rawMessage);
    if(res < 0){
      eof = 1;
      continue;
    }else if(res > 0){
      log_err("MRT table dump thread: at least one bad message encountered, throwing out records\n");
      int i;
      for(i=0;i<bgp_count_prev;i++){
        BGP_freeMessage(bgp_prev[i]);
      }
      bgp_count_prev = 0;
      free(bgp_prev);
      free(peer_idxs_prev);
    }

    if(mrtHeader->type != 13){
      log_err("mrtThread, an MRT message of type %d was recieved by the thread handling only 13 (TABLE) messages\n",mrtHeader->type);
      free(tablebuffer);
      return -1;
    }

    //       1    PEER_INDEX_TABLE
    //       2    RIB_IPV4_UNICAST
    //       3    RIB_IPV4_MULTICAST
    //       4    RIB_IPV6_UNICAST
    //       5    RIB_IPV6_MULTICAST
    //       6    RIB_GENERIC
    switch (mrtHeader->subtype){
      case 1:
        log_err("mrtThread, only one type 13 subtype 1 message is expected by this thread\n");
        free(tablebuffer);
        return -1;
      case 3:
      case 5:
        log_warning("mrtThread, unsupported table dump subtype\n");
        if(fastForward(cn->socket,mrtHeader->length)){
          eof = 1;
        }
        break;
      case 2:
      case 4:
#ifdef DEBUG
  log_msg("MRT: new specific message %d\n",debug_count);
#endif
        if(MRT_processType13SubtypeSpecific(mrtHeader,rawMessage,&bgp_arr,&peer_idxs,&bgp_count)){
          eof = 1;
        }
        break;
      case 6:
#ifdef DEBUG
  log_msg("MRT: new generic message %d\n",debug_count);
#endif
        if(MRT_processType13SubtypeGeneric(mrtHeader,rawMessage,&bgp_arr,&peer_idxs,&bgp_count)){
          eof = 1;
        }
        break;
      default:
        log_err("mrtThread, Invalid subtype for type 13 MRT message\n", mrtHeader->subtype);
        free(tablebuffer);
        return -1;
    }

    if(!eof){
      // if we had a valid return (meaning that bmf_count > 0 then process the previous bmf set
      if(bgp_count > 0){
        if(bgp_count_prev > 0){
          // in this case we have processed 2 valid messages in a row and we can now
          // add these results
          for(i=0;i<bgp_count_prev;i++){
            // needed: peer index, session id
            if(peer_idxs_prev[i] < 0 || peer_idxs_prev[i] >= indexPtr.PeerCount){
              log_err("mrt thread (13): attempting to reference a peer idx that is outside the range\n");
              return -1;
            }
            if(insertBGPTable(&(tablebuffer[peer_idxs_prev[i]]),bgp_prev[i])){
              log_err("error in type 13 table addition, i=%d, peer_idx=%d\n",i,peer_idxs_prev[i]);
              return -1;
            }
          }
          free(bgp_prev);
          free(peer_idxs_prev);
        }
        bgp_prev = bgp_arr;
        bgp_count_prev = bgp_count; 
        peer_idxs_prev = peer_idxs;
      }else{
        // we have to set all of this to zero because we don't know if the problem was within 
        // the current message or the previous message
        log_err("MRT table dump thread: at least one bad message encountered, throwing out records\n");
        int i;
        for(i=0;i<bgp_count_prev;i++){
          BGP_freeMessage(bgp_prev[i]);
        }
        bgp_count_prev = 0;
        free(bgp_prev);
        bgp_prev = NULL;
        free(peer_idxs_prev);
        peer_idxs_prev=NULL;
      }
    }
  }


  // get the last set of records
  for(i=0;i<bgp_count_prev;i++){
    if(insertBGPTable(&(tablebuffer[peer_idxs_prev[i]]),bgp_prev[i])){
      log_err("error in type 13 table addition (clean up), i=%d, peer_idx=%d\n",i,peer_idxs_prev[i]);
      return -1;
    }
  }
  free(bgp_prev);
  free(peer_idxs_prev);

  // the loop has ended --> this means that the MRT session is being closed through a shutdown or through an eof.
  close( cn->socket );
  log_msg("MRT table dump thread: preparing to exit\n");

  int tableloopflag=0;
  // 6 * 30 = 3 minutes to wait for update message. If no update message - erase all.
  // there is no need to do this if we are shutting down bgpmon.... in that case just proceed to cleanup
  while (tableloopflag<6 && cn->deleteMrt==FALSE )
  {	
    cn->lastAction = time(NULL);
    // check if all pointers are null. if yes, than all tables are sent, thus exit thread
    if (checkBGPTableEmpty(tablebuffer, indexPtr.PeerCount) == 1)
    {	
      log_msg("mrtThread, Table Transfer is empty, exit the MRT thread");
      break;
    }
    // update the last action time
    // go through the peer list
    int i;
    for( i=0; i<indexPtr.PeerCount; i++ )
    {	
      // check the state of ID:  if update message came, it will be changed to stateMrtEstablished
      if ( getSessionState(tablebuffer[i].ID) == stateMrtEstablished ) 
      {
        // check if session  ASN has changed to 2 byte
        if ( (tablebuffer[i].table_exist == 1) && (tablebuffer[i].start != NULL)   ) {
          cn->lastAction = time(NULL);
          log_msg("mrtThread, Session %d change ASN to %d, send entire table to PeerQueue",
                   tablebuffer[i].ID,getSessionASNumberLength(tablebuffer[i].ID));
          // write all messages for this peer
          writeBGPTableToQueue(tablebuffer[i].ID, &(tablebuffer[i].start), cn->qWriter, getSessionASNumberLength(tablebuffer[i].ID) );
          // free linked list
          log_msg("mrtThread, Table for Session %d sent", tablebuffer[i].ID);
          freeBGPTable(&tablebuffer[i]);
          tablebuffer[i].start = NULL;
          tablebuffer[i].tail = NULL;
        }else if (tablebuffer[i].table_exist == 0){
          // case when received table for a peer is null (tablebuffer[i].start == NULL)
          // and table_exist flag was not set to 1
          // delete entire table from our RIB-IN
          cn->lastAction = time(NULL);
          log_msg("No BGP messages received from MRT TABLE_DUMP_V2, I will delete RIB-IN table for Session %d", tablebuffer[i].ID);
          // change state to stateError
          setSessionState( getSessionByID(tablebuffer[i].ID), stateError, eventManualStop);
          // delete prefix table and attributes
          if (cleanRibTable( tablebuffer[i].ID)  < 0)
          {
            log_warning("Could not clear table for Session ID %d", tablebuffer[i].ID);
          }
          // skip deletion next loop
          tablebuffer[i].table_exist = 1;
          // free nothing
          freeBGPTable(&tablebuffer[i]);
          tablebuffer[i].start = NULL;
          tablebuffer[i].tail = NULL;
        }
      }
    }
    tableloopflag++;
    // sleep
    sleep(TABLE_TRANSFER_SLEEP);
  }

  cn->lastAction = time(NULL);
  // free peer table index
  for (i=0; i < indexPtr.PeerCount;i++)
  {
    // free linked list
    freeBGPTable(&tablebuffer[i]);
    tablebuffer[i].start = NULL;
    tablebuffer[i].tail = NULL;
  }
  indexPtr.PeerCount = 0;
  free(tablebuffer);
  return 1;
}	

/******************************************************************************
 * Name: MRT_createTableBufferFromType13Subtype1
 * Input: MrtNode, mrtindex, tablebuffer (to be allocated), rawMessage mrtHeader
 * Outout: 0 on sucess, -1 on failure
 * Description:
 * This function reads the first message in a rib table conversation
 * and creates the infrastructure for saving all of the other data to be 
 * receieved.
******************************************************************************/
int
MRT_createTableBufferFromType13Subtype1(MrtNode *cn, mrt_index *indexPtr,TableBuffer **tablebuffer, const uint8_t *rawMessage, MRTheader *mrtHeader)
{

  int idx = 0;
  char SrcIPString[ADDR_MAX_CHARS];
  // This is an MRT message type 13 subtype 1
  // It is a PEER_INDEX_TABLE

  // now we start reading the header
  // http://tools.ietf.org/search/draft-ietf-grow-mrt-17#section-4
  //         0                   1                   2                   3
  //    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |                      Collector BGP ID                         |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |       View Name Length        |     View Name (variable)      |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |          Peer Count           |    Peer Entries (variable)
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //
  //                Figure 5: PEER_INDEX_TABLE Subtype

  // collector BGP ID
  memmove(&indexPtr->BGPSrcID,&rawMessage[idx],sizeof(indexPtr->BGPSrcID)); 
  idx += sizeof(indexPtr->BGPSrcID);
  if( inet_ntop(AF_INET, &indexPtr->BGPSrcID, SrcIPString, ADDR_MAX_CHARS) == NULL ){
     log_err("mrtThread, TABLE DUMP v2 message: indexPtr->BGPSrcID ipv4 convert error!");
     return -1;
  }

  // View Name Length
  memmove(&indexPtr->ViewNameLen,&rawMessage[idx],sizeof(indexPtr->ViewNameLen));
  idx += sizeof(indexPtr->ViewNameLen);
  indexPtr->ViewNameLen = ntohs(indexPtr->ViewNameLen);
  if (indexPtr->ViewNameLen != 0)
  {
    memmove(&indexPtr->ViewName,&rawMessage[idx],indexPtr->ViewNameLen);
    idx += indexPtr->ViewNameLen;
  }

  // Peer Count
  memmove(&indexPtr->PeerCount,&rawMessage[idx],sizeof (indexPtr->PeerCount));
  idx += sizeof(indexPtr->PeerCount);
  indexPtr->PeerCount = ntohs (indexPtr->PeerCount);

  // allocate array of linked structure for each peer
  (*tablebuffer) = calloc( indexPtr->PeerCount,sizeof(TableBuffer));
  if (*tablebuffer == NULL)
  {
    log_err("Malloc failed");
    return -1;
  }

  // Now that the header has been read -- handle each entry
  //    0                   1                   2                   3
  //    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |   Peer Type   |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |                         Peer BGP ID                           |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |                   Peer IP address (variable)                  |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |                        Peer AS (variable)                     |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //
  //                      Figure 6: Peer Entries
  int i;
  for (i = 0; i < (indexPtr->PeerCount); i++)
  {
    // Peer Type
    // the peer type will tell us we we have ipv4 or ipv6 IP addresses
    //The Peer Type field is a bit field which encodes the type of the AS
    //and IP address as identified by the A and I bits, respectively,
    //below.
    //
    // 0 1 2 3 4 5 6 7
    //+-+-+-+-+-+-+-+-+
    //| | | | | | |A|I|
    //+-+-+-+-+-+-+-+-+
    //
    //Bit 6: Peer AS number size:  0 = 16 bits, 1 = 32 bits
    //Bit 7: Peer IP Address family:  0 = IPv4,  1 = IPv6
    //
    //                 Figure 7: Peer Type Field
    indexPtr->PeerType = rawMessage[idx];
    idx++;
			
    // Peer BGP ID 
    memmove(&indexPtr->PeerBGPID,&rawMessage[idx],sizeof (indexPtr->PeerBGPID));

    // Peer IP address: the size depends on if it is IPV4 or 6 (see note above)
    if ( (indexPtr->PeerType & 0x01) == 0) 
    {
      memmove(&indexPtr->PeerIP,&rawMessage[idx],4);
      idx+=4;
      if( inet_ntop(AF_INET, &indexPtr->PeerIP, indexPtr->PeerIPStr, ADDR_MAX_CHARS) == NULL ){
        perror("INET_NTOP ERROR:");
        log_err("mrtThread, TABLE DUMP v2 message: ipv4 source address convert error!");
        free(*tablebuffer);
        *tablebuffer = NULL;
        return -1;
      }
    }else{ // indexPtr.PeerType & 0x01) == 1
      memmove(&indexPtr->PeerIP,&rawMessage[idx],16);
      if( inet_ntop(AF_INET6, &indexPtr->PeerIP, indexPtr->PeerIPStr, ADDR_MAX_CHARS) == NULL ){
        perror("INET_NTOP ERROR:");
        log_err("mrtThread, TABLE DUMP v2 message: ipv6 source address convert error!");
        free(*tablebuffer);
        tablebuffer = NULL;
        return -1;
      }				
    }

    // check 2nd bit - ASN2 or ASN4
    int ASNumLen = 4;
    if ( (indexPtr->PeerType & 0x02) == 0)
    {
      // set AS Num Len to 2 byte
      ASNumLen = 2;
    }
    // PeerAS
    memmove(&indexPtr->PeerAS,&rawMessage[idx],ASNumLen);
    idx+=ASNumLen;
    if(ASNumLen == 2){
      indexPtr->PeerAS = ntohs (indexPtr->PeerAS);
    }else{
      indexPtr->PeerAS = ntohl (indexPtr->PeerAS);
    }

    // at this point the index table entry has been read. 
    // the next step is to look up the associated session.
    // in the case that a session does not exist, a temporary session is created.
    // A temporary session is required, until we recieve an update message with the associated
    // session that indicates if we should be using 2 byte or 4 byte ASNs in the table.
    // the MRT collector promotes ASNs to 4 bytes and so at this point we cannot know which to use.
					

    // TODO: known problem: the local ip address here is wrong.
    // the value we are using is the ip address that the MRT collector is using to communicate with 
    // us, but may not (and in the case of IPV6 is not) the IP address that it uses to communicate with
    // its peers. This means that we will never find the same session as the update messages (type 16).
    // this bug is issue number BGPMON-28 in Jira
    // find a session based on six tuples
    int sessionID = findSession(  indexPtr->PeerAS, 6447, 179, 179, indexPtr->PeerIPStr, SrcIPString);
    if( sessionID < 0)
    {
      // create a new session
      (*tablebuffer)[i].ID = createMrtSessionStruct(  indexPtr->PeerAS, 6447, indexPtr->PeerIPStr, SrcIPString, cn->labelAction, UNKASNLEN);
      // This is an MRT message type 13 subtype 1
      if( (*tablebuffer)[i].ID < 0 )
      {
        log_err("mrtThread (13), fail to create a new session for AS 6447,AS %d, SRC %s, DST %s",  
                  indexPtr->PeerAS, indexPtr->BGPSrcID, indexPtr->PeerIPStr );
        free(*tablebuffer);
        *tablebuffer = NULL;
        return -1;
      }else{
        log_msg("mrtThread (13), new session for AS 6447, AS %d, SRC %s, DST %s, ASNumLen %d is created",
                  indexPtr->PeerAS, SrcIPString, indexPtr->PeerIPStr,UNKASNLEN);
      }
      // set Session state as stateERROR
      setSessionState(getSessionByID((*tablebuffer)[i].ID), stateError, eventManualStop);
    }else{
      // store all SessionID's in array, we will need it in new BMF messages
      (*tablebuffer)[i].ID = sessionID;
    }
  } // end of Peer Entries Loop
  log_msg("successful completion of 1st message\n");
  return 0;
}

/*-------------------------------------------------------------------------------------
 * Purpose:
 * Input:
 * Output:
 * Info:
 * 4.3.3. RIB_GENERIC Subtype


   The RIB_GENERIC header is shown below.  It is used to cover RIB
   entries which do not fall under the common case entries defined
   above.  It consists of an AFI, Subsequent AFI (SAFI) and a single
   NLRI entry.  The NLRI information is specific to the AFI and SAFI
   values.  An implementation which does not recognize particular AFI
   and SAFI values SHOULD discard the remainder of the MRT record.

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                         Sequence number                       |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |    Address Family Identifier  |Subsequent AFI |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |     Network Layer Reachability Information (variable)         |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |         Entry Count           |  RIB Entries (variable)
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

                    Figure 9: RIB_GENERIC Entry Header
---------------------------------------------------------------------------------------*/
int 
MRT_processType13SubtypeGeneric(MRTheader *mrtHeader,const uint8_t *rawMessage, BGPMessage ***bgp_arr,int **peer_idxs,int *bgp_count) 
{
  log_warning("mrtThread, generic table table dump message ignored\n");
  return 0;
}

/*--------------------------------------------------------------------------------------
 * Purpose: this code processes MRT messages of type 13 subtype 2-5 (inclusive)
 * Input:  MrtNode, mrt_index,subtype 
 * Output: 0 for success
 *         -1 for failure, this may be a read error or a format error
 *           therefore, it should not cause the read loop to exit
 * Description: The main job of this function is to take the rawMessage, which is 
 * in MRT format, change it to BGP format, and then wrap it with a BMF header.
 * There will be one bmf message for each rib entry in the MRT message
--------------------------------------------------------------------------------------*/
int 
MRT_processType13SubtypeSpecific(MRTheader *mrtHeader,const uint8_t *rawMessage, BGPMessage ***bgp_arr,int **peer_indexes,int *bgp_count) 
{

  int idx =0; // this variable keeps track of our progress walking through rawMessage
  uint16_t afi;
  uint8_t safi;

  // max prefix length in bytes (IPV6 = 16, IPV4 = 4)
  // we can get this from the subtype
  int max_prefix_len = 0;
  switch (mrtHeader->subtype ){
    case 2: //RIB_IPV4_UNICAST
      max_prefix_len = 4;
      afi = 1;
      safi = 1;
      break;
    case 3: //RIB_IPV4_MULTICAST
      max_prefix_len = 4;
      afi = 1;
      safi = 2;
      break;
    case 4: //RIB_IPV6_UNICAST
      max_prefix_len = 16;
      afi = 2;
      safi = 1;
      break;
    case 5: //RIB_IPV6_MULTICAST
      max_prefix_len = 16;
      afi = 2;
      safi = 2;
      break;
    default:
      log_err("mrtThread, invalid subtype\n");
      return -1;
  } 

  // space to save the message data
  mrt_uni uni; 

  // 4.3.2. AFI/SAFI specific RIB Subtypes
  //
  //
  //   The AFI/SAFI specific RIB Subtypes consist of the RIB_IPV4_UNICAST,
  //   RIB_IPV4_MULTICAST, RIB_IPV6_UNICAST, and RIB_IPV6_MULTICAST
  //   Subtypes.  These specific RIB table entries are given their own MRT
  //   TABLE_DUMP_V2 subtypes as they are the most common type of RIB table
  //   instances and providing specific MRT subtypes for them permits more
  //   compact encodings.  These subtypes permit a single MRT record to
  //   encode multiple RIB table entries for a single prefix.  The Prefix
  //   Length and Prefix fields are encoded in the same manner as the BGP
  //   NLRI encoding for IPV4 and IPV6 prefixes.  Namely, the Prefix field
  //   contains address prefixes followed by enough trailing bits to make
  //   the end of the field fall on an octet boundary.  The value of
  //   trailing bits is irrelevant.
  //
  //        0                   1                   2                   3
  //        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                         Sequence number                       |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       | Prefix Length |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                        Prefix (variable)                      |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |         Entry Count           |  RIB Entries (variable)
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //
  //                        Figure 8: RIB Entry Header
		
  // Sequence number
  MRT_READ_4BYTES(uni.SeqNumb,rawMessage[idx]);
  idx+=4;

  // Prefix Length
  uni.PrefixLen = rawMessage[idx];
  idx+=1;

  // validate the prefix length
  if(uni.PrefixLen < 0 || uni.PrefixLen > max_prefix_len*8){
    log_err("mrtThread, the prefix length is not valid for this type \n");
    return 0;
  }

  // Prefix
  int i;
  for(i=0; i<uni.PrefixLen/8;i++){
    uni.Prefix[i] = rawMessage[idx];
    idx++;
  }
  if(uni.PrefixLen%8){
    uni.Prefix[i] = rawMessage[idx];
    idx++;
  }

  // Entry Count
  MRT_READ_2BYTES(uni.EntryCount,rawMessage[idx]);
  idx +=2;
 
  // each entry will result in a bgp message being added to the array
  (*bgp_arr) = calloc(uni.EntryCount,sizeof(BGPMessage*));
  (*peer_indexes) = calloc(uni.EntryCount,sizeof(int));
  (*bgp_count) = 0;

  // RIB Entries
  // 4.3.4. RIB Entries
  //
  //
  //   The RIB Entries are repeated Entry Count times.  These entries share
  //   a common format as shown below.  They include a Peer Index from the
  //   PEER_INDEX_TABLE MRT record, an originated time for the RIB Entry,
  //   and the BGP path attribute length and attributes.  All AS numbers in
  //   the AS_PATH attribute MUST be encoded as 4-Byte AS numbers.
  //
  //        0                   1                   2                   3
  //        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |         Peer Index            |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                         Originated Time                       |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |      Attribute Length         |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                    BGP Attributes... (variable)
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //
  //                          Figure 10: RIB Entries
  //
  //   There is one exception to the encoding of BGP attributes for the BGP
  //   MP_REACH_NLRI attribute (BGP Type Code 14) RFC 4760 [RFC4760].  Since
  //   the AFI, SAFI, and NLRI information is already encoded in the
  //   MULTIPROTOCOL header, only the Next Hop Address Length and Next Hop
  //   Address fields are included.  The Reserved field is omitted.  The
  //   attribute length is also adjusted to reflect only the length of the
  //   Next Hop Address Length and Next Hop Address fields.
#ifdef DEBUG
log_msg("MRT type specific message: %d entry count\n",uni.EntryCount);
#endif
  int j;
  for (j = 0; j < uni.EntryCount; j++)
  {
    // each of these will result in an BGP message being created
    // each of them will be an update message
    BGPMessage *bgpMessage = BGP_createMessage(BGP_UPDATE);

    // Peer Index
    MRT_READ_2BYTES(uni.PeerIndex,rawMessage[idx]);
    idx+=2;
 
    // Originating Time 
    MRT_READ_4BYTES(uni.OrigTime,rawMessage[idx]);
    idx+=4;

    // Attribute Length	
    MRT_READ_2BYTES(uni.AttrLen,rawMessage[idx]);
    idx+=2;
    int attr_start_pos = idx; // keep track of where the attributes start
                              // so we know when the are finished
  
    // BGP Attributes
    // 
#ifdef DEBUG
log_msg("MRT type specific message: %d attrlen\n",uni.AttrLen);
#endif
    while(idx-attr_start_pos < uni.AttrLen){
      // type is handled special because it is actually a 2 octed data type and not 
      // a value to be combined using ntohs.
      uint8_t flags = rawMessage[idx]; // note that this byte is read twice
      uint8_t code  = rawMessage[idx+1]; // note that this byte is read twice
      idx+=2;

      // If the Extended Length bit of the Attribute Flags octet is set
      // to 0, the third octet of the Path Attribute contains the length
      // of the attribute data in octets.
      //
      // If the Extended Length bit of the Attribute Flags octet is set
      // to 1, the third and fourth octets of the path attribute contain
      // the length of the attribute data in octets.
      int len = BGP_PAL_LEN(flags);
      uint16_t length;
      if(len == 1){
        length = rawMessage[idx];
        idx++;
      }else{
        MRT_READ_2BYTES(length,rawMessage[idx]);
        idx+=2;
      }
    
      // this is a special case ==> MP_REACH_NLRI
      // for this one we need to do some editing to the attributes
      // There is one exception to the encoding of BGP attributes for the BGP
      // MP_REACH_NLRI attribute (BGP Type Code 14) RFC 4760 [RFC4760].  Since
      // the AFI, SAFI, and NLRI information is already encoded in the
      // MULTIPROTOCOL header, only the Next Hop Address Length and Next Hop
      // Address fields are included.  The Reserved field is omitted.  The
      // attribute length is also adjusted to reflect only the length of the
      // Next Hop Address Length and Next Hop Address fields.

      // TODO: remove the 14 and put in #define
      if(code == 14){
        // this is what we want
        //+---------------------------------------------------------+
        //| Address Family Identifier (2 octets)                    |
        //+---------------------------------------------------------+
        //| Subsequent Address Family Identifier (1 octet)          |
        //+---------------------------------------------------------+
        //| Length of Next Hop Network Address (1 octet)            |
        //+---------------------------------------------------------+
        //| Network Address of Next Hop (variable)                  |
        //+---------------------------------------------------------+
        //| Reserved (1 octet)                                      |
        //+---------------------------------------------------------+
        //| Network Layer Reachability Information (variable)       |
        //+---------------------------------------------------------+

        // this is what we have
        //+---------------------------------------------------------+
        //| Length of Next Hop Network Address (1 octet)            |
        //+---------------------------------------------------------+
        //| Network Address of Next Hop (variable)                  |
        //+---------------------------------------------------------+

        // 48 bytes should be plenty of space for this attribute
        // TODO: magic number removal
        uint8_t newAtt[48];
        int i =0;
        // AFI and SAFI are determined by the type and subtype
        // look at the switch statement above to see them set
        BGP_ASSIGN_2BYTES(&newAtt[i],afi);
        i+=2;
        newAtt[i]=safi;
        i+=1;

        // Next Hop Length
        newAtt[i]=rawMessage[idx];
        i+=1; idx+=1;

        int j;
        for(j=0;j<newAtt[i-1];j++){
          newAtt[i+j]=rawMessage[idx+j];
        }
        i+=j;
        idx+=j;

        // Reserved
        i+=1;

        // NLRI -- there is only one here
        newAtt[i] = uni.PrefixLen;
        i+=1;
        for(j=0;j<uni.PrefixLen/8;j++){
          newAtt[i] = uni.Prefix[j];
          i+=1;
        } 
        if(uni.PrefixLen%8){
          newAtt[i] = uni.Prefix[j];
          i+=1;
        }
        length = i;
        // this is where the path attribute is added to the object 
        BGP_addPathAttributeToUpdate(bgpMessage,flags,code,length,newAtt);
      }else{ 
        // this is where the path attribute is added to the object 
        BGP_addPathAttributeToUpdate(bgpMessage,flags,code,length,&rawMessage[idx]);
        idx+=length; 
      }
    }
    // At this point this BGP message is complete... The next task is to convert it into a BMF 
    // we then need to give the BMF messages to the calling function
    (*bgp_arr)[*bgp_count] = bgpMessage;
    (*peer_indexes)[*bgp_count] = uni.PeerIndex;
    (*bgp_count)++;
  }
  return 0;
}

/*--------------------------------------------------------------------------------------
 * Purpose: this code processes MRT messages of type 16  BGP4MP subtype 1,4 UPDATE
 * Input:  MrtNode, the mrtheader object that put us here,
 * Output: 1 for success
 *         0 for failure
 * Description: This function will process a single MRT message and then return
--------------------------------------------------------------------------------------*/
int 
MRT_processType16SubtypeMessage(uint8_t *rawMessage,int asNumLen,MRTheader *mrtHeader,MRTmessage *mrtMessage,BMF *bmf)
{
  //4.4.2. BGP4MP_MESSAGE Subtype
  //
  //
  //   This Subtype is used to encode BGP messages.  It can be used to
  //   encode any Type of BGP message.  The entire BGP message is
  //   encapsulated in the BGP Message field, including the 16-octet marker,
  //   the 2-octet length, and the 1-octet type fields.  The BGP4MP_MESSAGE
  //   Subtype does not support 4-Byte AS numbers.  The AS_PATH contained in
  //   these messages MUST only consist of 2-Byte AS numbers.  The
  //   BGP4MP_MESSAGE_AS4 Subtype updates the BGP4MP_MESSAGE Subtype in
  //   order to support 4-Byte AS numbers.  The BGP4MP_MESSAGE fields are
  //   shown below:
  //
  //        0                   1                   2                   3
  //        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |         Peer AS number        |        Local AS number        |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |        Interface Index        |        Address Family         |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                      Peer IP address (variable)               |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                      Local IP address (variable)              |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                    BGP Message... (variable)
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //
  //4.4.3. BGP4MP_MESSAGE_AS4 Subtype
  //
  //
  //   This Subtype updates the BGP4MP_MESSAGE Subtype to support 4-Byte AS
  //   numbers.  The BGP4MP_MESSAGE_AS4 Subtype is otherwise identical to
  //   the BGP4MP_MESSAGE Subtype.  The AS_PATH in these messages MUST only
  //   consist of 4-Byte AS numbers.  The BGP4MP_MESSAGE_AS4 fields are
  //   shown below:
  //
  //        0                   1                   2                   3
  //        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                         Peer AS number                        |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                         Local AS number                       |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |        Interface Index        |        Address Family         |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                      Peer IP address (variable)               |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                      Local IP address (variable)              |
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //       |                    BGP Message... (variable)
  //       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  int idx = 0;
  int i;

  // Peer AS number 
  if(asNumLen ==2){
    MRT_READ_2BYTES(mrtMessage->peerAs,rawMessage[idx]);
    idx+=2;
  }else{
    MRT_READ_4BYTES(mrtMessage->peerAs,rawMessage[idx]);
    idx+=4;
  }
  if(idx > mrtHeader->length){
    log_err("MRT_processType16SubtypeMessage: idx:%lu exceeded length:%lu\n",idx, mrtHeader->length);
    return -1;
  }

 
  // Local As number
  if(asNumLen ==2){
    MRT_READ_2BYTES(mrtMessage->localAs,rawMessage[idx]);
    idx+=2;
  }else{
    MRT_READ_4BYTES(mrtMessage->localAs,rawMessage[idx]);
    idx+=4;
  }
  if(idx > mrtHeader->length){
    log_err("MRT_processType16SubtypeMessage: idx:%lu exceeded length:%lu\n",idx, mrtHeader->length);
    return -1;
  }

  // Interface Index
  MRT_READ_2BYTES(mrtMessage->interfaceIndex,rawMessage[idx]);
  idx+=2;
  if(idx > mrtHeader->length){
    log_err("MRT_processType16SubtypeMessage: idx:%lu exceeded length:%lu\n",idx, mrtHeader->length);
    return -1;
  }
  
  // Address Family
  MRT_READ_2BYTES(mrtMessage->addressFamily,rawMessage[idx]);
  idx+=2;
  if(idx > mrtHeader->length){
    log_err("MRT_processType16SubtypeMessage: idx:%lu exceeded length:%lu\n",idx, mrtHeader->length);
    return -1;
  }

  // Peer IP address
  int ip_len_bytes = 16;
  int af_inet = AF_INET6;
  if(mrtMessage->addressFamily == 1 ){
    ip_len_bytes = 4;
    af_inet = AF_INET;
  }
  for(i=0;i<ip_len_bytes;i++){
    mrtMessage->peerIPAddress[i] = rawMessage[idx];
    idx+=1;
  }
  if( inet_ntop(af_inet, mrtMessage->peerIPAddress,mrtMessage->peerIPAddressString,ADDR_MAX_CHARS) == NULL )
  {
    log_err("mrtThread, read BGP4MP_MESSAGE message: peer address convert error!");
    return -1;
  }
  if(idx > mrtHeader->length){
    log_err("MRT_processType16SubtypeMessage: idx:%lu exceeded length:%lu\n",idx, mrtHeader->length);
    return -1;
  }

  // Local IP address
  for(i=0;i<ip_len_bytes;i++){
    mrtMessage->localIPAddress[i] = rawMessage[idx];
    idx+=1;
  }
  if( inet_ntop(af_inet, mrtMessage->localIPAddress,mrtMessage->localIPAddressString,ADDR_MAX_CHARS) == NULL )
  {
    log_err("mrtThread, read BGP4MP_MESSAGE message: peer address convert error!");
    return -1;
  }
  if(idx > mrtHeader->length){
    log_err("MRT_processType16SubtypeMessage: idx:%lu exceeded length:%lu\n",idx, mrtHeader->length);
    return -1;
  }

  // All but the BGP message itself has been read and so now we need to see if we can
  // read the BGP raw message and make BMF message
  int bgp_length = mrtHeader->length - idx;
  if(bgp_length < 0){
    log_err("MRT_processtype16SubtypeMessage: the length is calculated to be less than 0\n");
    return -1;
  }
  if(bgp_length <= BGP_HEADER_LEN){
    log_err("MRT_processtype16SubtypeMessage: the length is calculated to be less than the bgp header length %d\n",bgp_length);
    return -1;
  }
  int tmpIdx = idx;
  for(;tmpIdx<16;tmpIdx++){
    if(rawMessage[tmpIdx] != 0xff){
      log_err("MRT_processtype16SubtypeMessage: Unable to continue with message, BGP marker not found where expected\n");
      return -1;
    }
  }

  (*bmf) = createBMF(0,  BMF_TYPE_MSG_FROM_PEER);
  if(bgpmonMessageAppend( (*bmf), &rawMessage[idx], bgp_length)){
    log_err("MRT_processType16SubtypeMessage: Unable to submit message\n");
    return -1;
  }
  return 0;
}

/*--------------------------------------------------------------------------------------
 * Purpose: this code attaches a session id to the BMF and submits it to the Q.
 * Input:  mrtHeader,bmf object
 * Output: 1 for success
 *         0 for failure
 * Description: 
--------------------------------------------------------------------------------------*/
int 
submitBMF(MrtNode *cn, MRTheader *mrtHeader, MRTmessage *mrtMessage, BMF bmf)
{
  int sessionID;
  int asNumLen = 4;
  if(mrtHeader->subtype == 1){
    asNumLen = 2;
  }
 
  // get the session id and set it in the bmf message
  sessionID = findSession(mrtMessage->peerAs,mrtMessage->localAs,179,179,
                               mrtMessage->peerIPAddressString,mrtMessage->localIPAddressString);		
  if( sessionID < 0)
  {
    // create a new session
    sessionID = createMrtSessionStruct(mrtMessage->peerAs,mrtMessage->localAs,
                                       mrtMessage->peerIPAddressString,mrtMessage->localIPAddressString,
                                       cn->labelAction, asNumLen);
    if( sessionID < 0 )
    {
      log_err( "mrtThread (submitBMF), failed to create a new session for %lu,%lu, %s, %s",
                                        mrtMessage->peerAs,mrtMessage->localAs, 
                                        mrtMessage->peerIPAddressString,mrtMessage->localIPAddressString);		
      destroyBMF(bmf);
      bmf = NULL;
      return -1;
    }else{
      log_msg( "mrtThread (submitBMF), new session for AS %lu, AS %lu, SRC %s, DST %s, ASNumLen %d is created",
                                        mrtMessage->peerAs,mrtMessage->localAs, 
                                        mrtMessage->peerIPAddressString,mrtMessage->localIPAddressString,
                                        asNumLen);		
    }
  }
  // if state has changed (node disconnected and connected back)
  if (getSessionState(sessionID) != stateMrtEstablished)
  {	
    setSessionState(getSessionByID(sessionID), stateMrtEstablished, eventNone);
  }

  // write the message to the queue
  bmf->sessionID = (uint16_t)sessionID;
  writeQueue( cn->qWriter, bmf );
  incrementSessionMsgCount(sessionID);
  return 1;
}
