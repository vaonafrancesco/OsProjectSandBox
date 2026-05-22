 #ifndef ROUTING_H
 #define ROUTING_H

 #include <stdbool.h>
 #include "protocol.h"

 // Maximum number of devices supported by the system
 #define MAX_DEVICES 128

 //device types: used to prevent bulb, window, or fridge from becoming parents

 typedef enum {
    DEV_CONTROLLER,
    DEV_HUB,
    DEV_TIMER,
    DEV_BULB,
    DEV_WINDOW,
    DEV_FRIDGE,
    DEV_UNKNOWN
 } device_type;

 //routing node: represents a single device inside the hierarchy routing table.
 typedef struct{
    int id;
    int parent_id;      //the id of the logical parent (e.g., 0 for Controller)
    device_type type;   // Used to distinguish between Control and Interaction devices
 }routing_node;

 // shared state

 // global routing table. defined in routing.c, accessed by hierarchy.c
 extern routing_node routing_table[MAX_DEVICES];

 //routing table management (from routing.c)

 void routing_init(void);
 int routing_add_node(int id, device_type type);
 int routing_remove_node(int id);

 // hierarchy & linking logic (from hierarchy.c)

 bool is_control_device(device_type type);
 int routing_link_devices(int child_id, int parent_id);


 #endif