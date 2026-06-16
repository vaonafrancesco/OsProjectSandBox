#include <stdlib.h>
#include <unistd.h>

#include "utils.h"
#include "common.h"


 void simulate_random_delay(void) {
    int delay = MIN_RANDOM_DELAY_S;
    delay += rand()% (MAX_RANDOM_DELAY_S - MIN_RANDOM_DELAY_S+ 1);
    sleep((unsigned int)delay);
}