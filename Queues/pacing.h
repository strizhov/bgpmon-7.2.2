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
 *  File: pacing.h
 * 	Authors: He Yan, Dan Massey
 *  Date: Oct 7, 2008 
 */

#ifndef PACING_H_
#define PACING_H_

/* need the queue data structure */
#include "queue.h"

/*--------------------------------------------------------------------------------------
 * Purpose: apply pacing rules to this queue
 * Input:   the queue and writer (identified by index) to apply pacing rules
 * Output:   0 on success,  1 on error
 * Dan Massey @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
applyPacing(Queue q, int writerindex);

/*--------------------------------------------------------------------------------------
 * Purpose: Check if need to stop pacing for this queue
 * Input: queue to check
 * Output: 0 on success, 1 on error
 * Dan Massey @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
checkPacingStop(Queue q);

/*--------------------------------------------------------------------------------------
 * Purpose: calculate the number of allowed writes per interval for each writer to this queue
 * Input:  the queue that needs a new limit
 * Output:   returns 0 on success, 1 on error
 *           sets the queue's writesLimit parameter
 * Dan Massey @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
int 
calculateWritesLimit (Queue q);

/*--------------------------------------------------------------------------------------
 * Purpose: Check if a new interval starts. If so, update writes limit and reset readcout and writecount.
 * Input:  the queue that needs to update interval
 * Output:
 * He Yan @ July 22, 2008
 * -------------------------------------------------------------------------------------*/
void updateInterval( Queue q );

#endif /*PACING_H_*/
