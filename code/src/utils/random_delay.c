#include <stdlib.h>
#include <unistd.h>
#include "common.h"

void domo_simulate_delay(void){
    int delay = DOMO_MIN_RANDOM_DELAY_S;
    if(DOMO_MAX_RANDOM_DELAY_S > DOMO_MIN_RANDOM_DELAY_S){
        delay += rand() % (DOMO_MAX_RANDOM_DELAY_S - DOMO_MIN_RANDOM_DELAY_S + 1);
    }
    delay = (unsigned int)delay;
    sleep(delay);
}