/* The serial driver depends on counting semaphores */
#define configUSE_COUNTING_SEMAPHORES 1

#define configCHECK_FOR_STACK_OVERFLOW 2

/* Use the defaults for everything else */
#include_next<FreeRTOSConfig.h>
