#ifndef DEVICE_H
#define DEVICE_H

#include "common.h"
#include "ipc.h"

typedef enum {
    DEVICE_CONTROLLER = 0,
    DEVICE_HUB,
    DEVICE_TIMER,
    DEVICE_BULB,
    DEVICE_WINDOW,
    DEVICE_FRIDGE, 
    DEVICE_UNKNOWN
} device_type ;

typedef struct {
    device_id id;
    device_type type;
    pid_t pid;
    device_id logical_parent_id;
    state state ;
    bool manual_override ;
    char fifo_path[PATH_MAX];
    char name[NAME_MAX] ;
} device_info ;

typedef struct device device ;

typedef int (*device_init)(device *dev) ;
typedef int (*device_handle)( device *dev,const domo_message *req,domo_message *resp);
typedef int (*device_destroy)(device *dev);

struct device_impl;


struct device {
    device_info info;

    size_t child_count;
    size_t child_capacity;
    device_id *child_ids;




    char *registry_snapshot ;
    size_t registry_snapshot_size;

    device_init init;
    device_handle handle_message;
    device_destroy destroy;

    struct device_impl *impl;
};

const char *device_type_str( device_type type) ;
bool device_is_control(device_type type );
bool device_is_interaction(device_type type);
      
int device_build_info_payload (const device *dev, char *buffer, size_t buffer_len);

int device_apply_switch(device *dev,const char *label,const char *position);
int device_set_parameter(device *dev , const char *key ,const char *value);

int device_common_init(device *dev, device_id id, device_type type);
int device_common_setup_fifo ( device *dev);

int device_common_open_fifo (device *dev, int *fd_out , int *dummy_fd_out);
int device_common_main_loop (device *dev, int fd) ;
int device_common_cleanup ( device *dev, int fd, int dummy_fd) ;


#endif