#ifndef _PORT_H_
#define _PORT_H_

#include "conf.h"

/**
 * Configure and start the port
 * 
 * @return
 *   - 0: Success
 *   - -1: Otherwise
 */
int init_port(void);

/**
 * Initilize the Flow Director
 */
void init_flow(void);

#endif