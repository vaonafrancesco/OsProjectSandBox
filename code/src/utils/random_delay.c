#include <stdlib.h>
#include <unistd.h>

#include "utils.h"
#include "common.h"
	
// Simulates the time it takes for a real hardware device to physically react
 void simulate_random_delay(void) {
 	// Start with the minimum required delay
    int delay = MIN_RANDOM_DELAY_S;
    // Add a random amount of extra time on top of it
    delay += rand()% (MAX_RANDOM_DELAY_S - MIN_RANDOM_DELAY_S+ 1);
    // Put the process to sleep for the calculated amount of seconds.
    sleep((unsigned int)delay);
}
