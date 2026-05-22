 #ifndef ROUTING_H
 #define ROUTING_H

 #include <stdbool.h>
 #include "protocol.h"
 #include "device.h"
 // Maximum number of devices supported by the system
 #define MAX_DEVICES 128

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

  //Helper to convert between device_type and string for routing(I still have to implement this)
 const char *device_typde_to_str(device_type type);

 // Helper functions for request-reply pattern (used by controller and devices)
 int make_device_fifo_path(device_id id, char *path, size_t path_len);
 int make_reply_fifo_path(pid_t pid, int request_id, char *path, size_t path_len);
 int request_reply(const char *target_fifo, const char *reply_fifo,
                  const domo_message *request, domo_message *response);
 int send_message_to_fifo(const char *fifo_path, const domo_message *msg);



 #endif