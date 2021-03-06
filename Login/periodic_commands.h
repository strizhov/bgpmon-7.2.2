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
 *  File: periodic_commands.h
 * 	Authors: Kevin Burnett, Dan Massey
 *
 *  Date: November 10, 2008
 */
 
#ifndef PERIODIC_COMMANDS_H_
#define PERIODIC_COMMANDS_H_

#include "commandprompt.h"

// periodic commands
int cmdPeriodicRouteRefresh(commandArgument * ca, clientThreadArguments * client, commandNode * root);
int cmdPeriodicStatusMessage(commandArgument * ca, clientThreadArguments * client, commandNode * root);
int cmdPeriodicRouteRefreshEnableDisable(commandArgument * ca, clientThreadArguments * client, commandNode * root);

// periodic show commands
int cmdShowPeriodicRouteRefresh(commandArgument * ca, clientThreadArguments * client, commandNode * root);
int cmdShowPeriodicStatusMessage(commandArgument * ca, clientThreadArguments * client, commandNode * root);
int cmdShowPeriodicRouteRefreshStatus(commandArgument * ca, clientThreadArguments * client, commandNode * root);

#endif
