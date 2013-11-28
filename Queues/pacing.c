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
 *  File: pacing.c
 * 	Authors: He Yan, Dan Massey
 *  Date: Oct 7, 2008
 */

/* pacing function prototypes */
#include "pacing.h"

/* needed for queue data type */
#include "queue.h"

/* needed for QUEUE_MAX_ITEMS */
#include "../site_defaults.h"

/* needed for TRUE/FALSE macro */
#include "../Util/bgpmon_defaults.h"

/* needed for logging */
#include "../Util/log.h"

/* needed for system functions such as sleep */
#include <unistd.h>
/* needed for memory functions as memcpy and memset*/
#include <string.h>
/*  needed to lock structures */
#include <pthread.h>

//#define DEBUG

/*--------------------------------------------------------------------------------------
 * Purpose: apply pacing rules to this queue
 * Input:   the queue and writer (identified by index) to apply pacing rules
 * Output:   0 on success,  1 on error
 * Dan Massey @ July 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
applyPacing(Queue q, int writerindex)
{

	//  turn on pacing if queue size exceeds threshhold
	if ( ( (float)(q->tail - q->head)/(float)QUEUE_MAX_ITEMS ) >=  QueueConfig.pacingOnThresh)
	{
		if ( q->pacingFlag == FALSE )
			q->logPacingCount++;
		q->pacingFlag = TRUE;
	}
	
	// if not pacing, nothing more to do 
	if (q->pacingFlag == FALSE) 
		return 0;

	if( q->writeCounts[writerindex] >= q->writesLimit)
	{
		// this writer must pause until a new interval arrives before writing again                        
		if ( pthread_mutex_unlock( &q->queueLock ) )
                	log_fatal( "unlockQueue: failed");
		#ifdef DEBUG
		debug(__FUNCTION__, "Queue %s writer %d is paced for %d seccond with %d", q->name, writerindex, q->tick + QueueConfig.pacingInterval - time(NULL), q->writesLimit);
		#endif
		sleep( q->tick + QueueConfig.pacingInterval - time(NULL));
		if ( pthread_mutex_lock( &q->queueLock ) )
                	log_fatal( "lockQueue: failed");
	}

	return 0;

}

/*--------------------------------------------------------------------------------------
 * Purpose: Check if need to stop pacing for this queue
 * Input: queue to check 
 * Output: 0 on success, 1 on error
 * Dan Massey @ July 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
checkPacingStop(Queue q)
{
	if ( ( (float)(q->tail - q->head)/(float)QUEUE_MAX_ITEMS ) < QueueConfig.pacingOffThresh )
		q->pacingFlag = FALSE;
	return 0;
}


/*--------------------------------------------------------------------------------------
 * Purpose: calculate the number of allowed writes per interval for each writer to this queue
 * Input:  the queue that needs a new limit
 * Output:   returns 0 on success, 1 on error
 *           sets the queue's writesLimit parameter
 * Dan Massey @ July 22, 2008
 * -------------------------------------------------------------------------------------*/ 
int 
calculateWritesLimit (Queue q)
{
	if( q->readercount == 0)
	{
		// if there are no readers, leave the writelimit unchanged.
                // with no readers, the queue will be drained and
                // pacing will turn off.   the writelimit will not be used.
		return 0;
	}

	if( q->writercount == 0)
	{
		// if there are no writers, leave the writelimit unchanged.
                // with no writers, the queue will be drained and
                // pacing will turn off.   the writelimit will not be used.
		return 0;
	}

	// objective is to write at a pace that matches the average reader

	// calculate the average speed of a reader
	int averageReads = q->readCount/q->readercount;

	// calculate how much each writer can write if shared equall
	// note this function is called by a writer thus writercount >= 1
	int instantWritesLimit = averageReads/q->writercount;

	// combine the current instant value with past history, using an
	// exponential weighted moving average
	q->writesLimit = 
		// a percentage of the previous limit; 1-alpha in an EWMA
		(1 - QueueConfig.alpha) * q->writesLimit + 
		// a percentage of the number of items read last interval ;  alpha in an EWMA
		QueueConfig.alpha * instantWritesLimit;

	
	// add an upbound on the amount a writer can write
	int availableQueueItems = QUEUE_MAX_ITEMS - (q->tail - q->head);
	int upbound = availableQueueItems / 2 ;
	//  allow each writer to consume up to half the remaining queue
	if( q->writesLimit > upbound )
		q->writesLimit = upbound;

	// ensure each writer can write at least some minimum number of entries
	if( q->writesLimit < QueueConfig.minWritesPerInterval)
		q->writesLimit = QueueConfig.minWritesPerInterval;	

	//log_msg( "Queue %s: new writes limit %d", q->name, q->writesLimit);

	return 0;
};

/*--------------------------------------------------------------------------------------
 * Purpose: calculate EWMA writes of all writers per interval 
 * Input:  the queue that needs a new EWMA writes
 * Output:   returns 0 on success, 1 on error
 *           sets the queue's EWMA writes
 * He Yan @ Sep 19, 2009
 * -------------------------------------------------------------------------------------*/ 
int 
calculateEWMAwrites (Queue q)
{	
	if(q->writesEWMA == 0)
		q->writesEWMA = q->writeCount;
	else
		q->writesEWMA = (1 - QueueConfig.alpha) * q->writesEWMA + QueueConfig.alpha * q->writeCount;
	
	return 0;
}
/*--------------------------------------------------------------------------------------
 * Purpose: Check if a new interval starts. If so, update writes limit and reset readcout and writecount.
 * Input:  the queue that needs to update interval
 * Output: 
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/ 
void updateInterval( Queue q )
{
	int now =  time( NULL );
	if( now < q->tick + QueueConfig.pacingInterval)
		return;

	while( now >= q->tick + QueueConfig.pacingInterval )
	{
		q->tick = q->tick + QueueConfig.pacingInterval;
		if(q->newPacingEnable == FALSE)
		{
			calculateWritesLimit( q );
			#ifdef DEBUG
			float util = (float)(q->tail - q->head)/(float)QUEUE_MAX_ITEMS;
			debug(__FUNCTION__, "Queue:%f, PacingOn: %f, reader(%d) current writes limit:%d readcout:%d, writecount:%d, %d, %d, pacingscount:%d",
				util, QueueConfig.pacingOnThresh, q->pacingFlag, q->writesLimit, q->readCount, q->writeCounts[0], q->writeCounts[1], q->writeCounts[2], q->logPacingCount);
			#endif
			q->readCount = 0;
			memset(q->writeCounts, 0, sizeof(int)*MAX_QUEUE_WRITERS );
		}
		else
		{
			// update the position of ideal reader
			q->idealReaderPosition += q->writesEWMA;
			if(q->idealReaderPosition > q->tail)
				q->idealReaderPosition = q->tail;  
			// update the moving average
			calculateEWMAwrites( q );
#ifdef DEBUF
			log_msg("Now ideal reader is at %ld, head is %ld, tail is %ld, %d readers", q->idealReaderPosition, q->head, q->tail, q->readercount) ;
#endif
			// reset the write count
			q->writeCount = 0;
		}
	}
}

