#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "utils.h"
#include "routing.h"

typedef struct {
    device base;
    char begin_time[6];  // HH:MM
    char end_time[6];    // HH:MM
    bool manual_override;
} timer_device;

static int timer_update(device *dev);

static const char *state_str(state state) 
{
    switch(state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

static int __attribute__((unused)) validate_time_format(const char *time_str) {
    int hours,minutes;

    if(time_str==NULL || strlen(time_str)!=5) {
        return ERR_INVALID_PARAMETERS ;
    }

    if(time_str[ 2 ]!=':') {
        return ERR_INVALID_PARAMETERS;
    }

    if(sscanf(time_str,"%d:%d",&hours,&minutes)!=2) {
        return ERR_INVALID_PARAMETERS ;
    }

    if(hours<0 || hours>23) {
        return ERR_INVALID_PARAMETERS;
    }

    if(minutes<0 || minutes>59) {
        return ERR_INVALID_PARAMETERS ;
    }

    return OK;
}

static int __attribute__((unused)) compare_times(const char *time1,const char *time2) {
    int h1,m1,h2,m2;

    sscanf(time1,"%d:%d",&h1,&m1);
    sscanf(time2,"%d:%d",&h2,&m2);

    if(h1<h2) return -1;
    if(h1>h2) return 1;
    if(m1<m2) return -1;
    if(m1>m2) return 1;
    return 0;
}

static int __attribute__((unused)) check_time_in_past(const char *time_str) {
    time_t now;
    struct tm *tm_now;
    int target_hours,target_minutes;

    time(&now);
    tm_now=localtime(&now) ;

    sscanf(time_str,"%d:%d",&target_hours,&target_minutes);

    if(target_hours<tm_now->tm_hour) {
        return 1;  // past already
    }
    if(target_hours==tm_now->tm_hour && target_minutes<tm_now->tm_min) {
        return 1;
    }

    return 0;
}

static int timer_propagate_to_children(device *dev,const domo_message *req) 
{
   /*device_id children[MAX_DEVICES];
    int child_count=0;*/
    int rc;
    size_t i;

    if(dev==NULL || req==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }


    // forward to all children devices
    for(i=0;i< dev->child_count;i++) {

        device_id child_id = dev->child_ids[i];

        if (child_id<0) continue;

        domo_message child_req;
        char child_fifo[PATH_MAX];

        memset(&child_req,0,sizeof(child_req));
        child_req.kind=MSG_REQUEST ;
        snprintf(child_req.sender_id,sizeof(child_req.sender_id),"%d",dev->info.id);
        snprintf(child_req.command,sizeof(child_req.command),"%s",req->command) ;
        child_req.src_id=dev->info.id;
        child_req.dst_id= child_id;   
        child_req.target_id= child_id; 
        child_req.src_pid=getpid();
        child_req.request_id=(int)getpid() ;
        snprintf(child_req.arg1,sizeof(child_req.arg1),"%s",req->arg1) ;
        snprintf(child_req.arg2,sizeof(child_req.arg2),"%s",req->arg2);
        snprintf(child_req.payload,sizeof(child_req.payload),"%s",req->payload) ;

        
       if(make_device_fifo_path(child_id, child_fifo, sizeof(child_fifo)) != OK) {
            continue;
        }

        rc=send_message_to_fifo(child_fifo,&child_req) ;
        if(rc!=OK) {
            return rc;
        }
    }

    return OK;
}

static int timer_build_info_payload(timer_device *timer,char *buf,size_t len) {
    if(timer==NULL || buf==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    if(timer->manual_override) {
        snprintf(buf,len,"timer id=%d state=manual_override begin=%s end=%s",
                 timer->base.info.id,timer->begin_time,timer->end_time);
    } else {
        snprintf(buf,len,"timer id=%d state=%s begin=%s end=%s",
                 timer->base.info.id,state_str(timer->base.info.state),
                 timer->begin_time,timer->end_time) ;
    }

    return OK;
}

static int parse_child_id_payload(const char *payload, device_id *child_id_out)
{
    int id;

    if (payload == NULL || child_id_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (sscanf(payload, "%d", &id) != 1 || id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    *child_id_out = (device_id)id;
    return OK;
}

static int parse_link_parent_id(const domo_message *req, int *parent_id_out)
{
    int parent_id;

    if (req == NULL || parent_id_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (sscanf(req->payload, "parent,%d", &parent_id) != 1) {
        return ERR_INVALID_PARAMETERS;
    }

    if (parent_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    *parent_id_out = parent_id;
    return OK;
}

static int timer_handle_message(device *dev,const domo_message *req,domo_message *resp) 
{
    timer_device *timer=(timer_device *)dev;

    if(timer==NULL || req==NULL || resp==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }
    timer_update(dev);

    memset(resp,0,sizeof(*resp));
    resp->kind=MSG_RESPONSE ;
    snprintf(resp->command,sizeof(resp->command),"%s",req->command);
    snprintf(resp->sender_id,sizeof(resp->sender_id),"%d",timer->base.info.id) ;
    resp->src_id=timer->base.info.id;
    resp->dst_id=req->src_id ;
    resp->src_pid=getpid();
    resp->request_id=req->request_id ;
    resp->status=OK;

    simulate_random_delay();

    if(strcmp(req->command,CMD_INFO)==0) {
        return timer_build_info_payload(timer,resp->payload,sizeof(resp->payload));
    }

    if( strcmp(req->command,CMD_SWITCH)==0 ){
        int rc;

        rc=timer_propagate_to_children(dev,req) ;
        if(rc!=OK) {
            resp->status=rc ;
            snprintf(resp->payload,sizeof(resp->payload),"failed to propagate switch to children");
            return OK;
        }

        timer->manual_override=false ;
        timer->base.info.manual_override = false;

        if(strcmp(req->arg2,"on")==0) {
            timer->base.info.state=STATE_ON ;
        } else if(strcmp(req->arg2,"off")==0) {
            timer->base.info.state=STATE_OFF;
        } else {
            resp->status=ERR_INVALID_PARAMETERS;
            snprintf(resp->payload,sizeof(resp->payload),"invalid switch position");
            return OK;
        }

        snprintf(resp->payload,sizeof(resp->payload),
                 "timer %d switched %s",timer->base.info.id,state_str(timer->base.info.state)) ;
        return OK;
    }

    if(strcmp(req->command,CMD_STATUS)==0) {
        if(strncmp(req->payload,"manual_override,",16)==0) {
            timer->manual_override=true ;
            timer->base.info.manual_override = true;
        }
        return OK;
    }

    if(strcmp(req->command,"SET")==0) {
        // Validate and set begin time
        if(strcmp(req->arg1,"begin")==0) {
            if(validate_time_format(req->arg2)!=OK) {
                resp->status=ERR_INVALID_PARAMETERS;
                snprintf(resp->payload,sizeof(resp->payload),"invalid time format (HH:MM)");
                return OK;
            }
            
            snprintf(timer->begin_time,sizeof(timer->begin_time),"%.5s",req->arg2);
            snprintf(resp->payload,sizeof(resp->payload),"timer %d begin set to %s",timer->base.info.id,timer->begin_time);
            return OK;
        }

        // Validate and set end time
        if(strcmp(req->arg1,"end")==0) {
            if(validate_time_format(req->arg2)!=OK) {
                resp->status=ERR_INVALID_PARAMETERS;
                snprintf(resp->payload,sizeof(resp->payload),"invalid time format (HH:MM)");
                return OK;
            }
            
            snprintf(timer->end_time,sizeof(timer->end_time),"%.5s",req->arg2);
            snprintf(resp->payload,sizeof(resp->payload),"timer %d end set to %s",timer->base.info.id,timer->end_time);
            return OK;
        }

        resp->status=ERR_INVALID_PARAMETERS;
        snprintf(resp->payload,sizeof(resp->payload),"invalid set parameter (use begin or end)");
        return OK;
    }

    if (strcmp(req->command, CMD_CHILD_ADDED) == 0) {
        device_id child_id;
        int rc = parse_child_id_payload(req->payload, &child_id);

        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid child_added payload");
            return OK;
        }

        if (timer->base.child_ids == NULL || timer->base.child_capacity < 1) {
            resp->status = ERR_SYSTEM;
            snprintf(resp->payload, sizeof(resp->payload), "timer child storage not initialized");
            return OK;
        }

        timer->base.child_ids[0] = child_id;
        timer->base.child_count = 1;

        snprintf(resp->payload, sizeof(resp->payload),
                 "timer %d added child %d",
                 timer->base.info.id,
                 child_id);
        return OK;
    }

    if (strcmp(req->command, CMD_CHILD_REMOVED) == 0) {
        device_id child_id;
        int rc = parse_child_id_payload(req->payload, &child_id);

        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid child_removed payload");
            return OK;
        }

        if (timer->base.child_ids != NULL &&
            timer->base.child_count == 1 &&
            timer->base.child_ids[0] == child_id) {
            timer->base.child_ids[0] = NO_PARENT;
            timer->base.child_count = 0;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                 "timer %d removed child %d",
                 timer->base.info.id,
                 child_id);
        return OK;
    }

    if (strcmp(req->command, CMD_LINK) == 0) {
        int parent_id;
        int rc = parse_link_parent_id(req, &parent_id);

        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid link payload");
            return OK;
        }

        timer->base.info.logical_parent_id = parent_id;
        snprintf(resp->payload, sizeof(resp->payload),
                 "timer %d linked to parent %d",
                 timer->base.info.id,
                 parent_id);
        return OK;
    }

    resp->status=ERR_INVALID_COMMAND ;
    snprintf(resp->payload,sizeof(resp->payload),"unknown command");
    return OK;
}

static int timer_init(device *dev) {
    timer_device *timer = (timer_device *)dev;

    if (timer == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    timer->manual_override = false;
    timer->base.info.state = STATE_OFF;
    timer->base.info.manual_override = false;
    strcpy(timer->begin_time, "00:00");
    strcpy(timer->end_time, "00:00");

    timer->base.child_capacity = 1;
    timer->base.child_count = 0;
    timer->base.child_ids = malloc(sizeof(device_id));
    if (timer->base.child_ids == NULL) {
        return ERR_SYSTEM;
    }
    timer->base.child_ids[0] = NO_PARENT;

    return OK;
}

static int timer_destroy(device *dev) {
    timer_device *timer = (timer_device *)dev;

    if (timer == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    free(timer->base.child_ids);
    timer->base.child_ids = NULL;
    timer->base.child_count = 0;
    timer->base.child_capacity = 0;

    return OK;
}


static int timer_get_child_device_type(device *dev, device_type *type_out) {
    timer_device *timer = (timer_device *)dev;
    domo_message req, resp;
    char target_fifo[PATH_MAX];
    char reply_fifo[PATH_MAX];
    int rc;

    if (dev == NULL || type_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (timer->base.child_count == 0) {
        return ERR_INVALID_PARAMETERS;
    }

    device_id child_id = timer->base.child_ids[0];

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_INFO);
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", dev->info.id);
    req.src_id = dev->info.id;
    req.dst_id = child_id;
    req.target_id = child_id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    req.status = OK;

    rc = make_device_fifo_path(child_id, target_fifo, sizeof(target_fifo));
    if (rc != OK) {
        return rc;
    }

    rc = make_reply_fifo_path(req.src_pid, req.request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != OK) {
        return rc;
    }

    rc = request_reply_timeout(target_fifo, reply_fifo, &req, &resp, TIMEOUT_DEVICE);
    if (rc != OK || resp.status != OK) {
        return rc;
    }

    if (strstr(resp.payload, "bulb id=") != NULL) {
        *type_out = DEVICE_BULB;
    } else if (strstr(resp.payload, "window id=") != NULL) {
        *type_out = DEVICE_WINDOW;
    } else if (strstr(resp.payload, "fridge id=") != NULL) {
        *type_out = DEVICE_FRIDGE;
    } else {
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    return OK;
}

static int timer_update(device *dev){
    timer_device *timer = (timer_device *)dev;

    if (timer == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if (strcmp(timer->begin_time, "00:00")== 0 && strcmp(timer->end_time, "00:00")==0){
        return OK;
    }
    if (timer->manual_override) {
        return OK;
    }

    time_t now;
    struct tm *tm_now;
    char current_time_str[6];


    time(&now);
    tm_now = localtime(&now);
    snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d",tm_now->tm_hour, tm_now->tm_min);

    bool is_on =false;

    if(compare_times(timer->begin_time, timer->end_time) <0){
        if (compare_times(current_time_str, timer->begin_time) >= 0 && compare_times(current_time_str, timer->end_time)<0){
            is_on = true;
        }
    }else if (compare_times(timer->begin_time, timer->end_time)>0){
        if (compare_times(current_time_str, timer->begin_time)>=0 || compare_times(current_time_str, timer->end_time)<0){
            is_on = true;
        }
    }

    state target_state = is_on ? STATE_ON : STATE_OFF;

    if (timer->base.info.state != target_state && timer->base.child_count > 0){
        domo_message auto_req;
        device_type child_type;
        int rc;

        rc = timer_get_child_device_type(dev, &child_type);
        if (rc != OK) {
            return rc;
        }

        memset(&auto_req, 0, sizeof(auto_req));

        snprintf(auto_req.command, sizeof(auto_req.command), "%s", CMD_SWITCH);

        switch (child_type) {
            case DEVICE_BULB:
                snprintf(auto_req.arg1, sizeof(auto_req.arg1), "power");
                snprintf(auto_req.arg2, sizeof(auto_req.arg2), is_on ? "on" : "off");
                break;
            case DEVICE_WINDOW:
            case DEVICE_FRIDGE:
                snprintf(auto_req.arg1, sizeof(auto_req.arg1), is_on ? "open" : "close");
                snprintf(auto_req.arg2, sizeof(auto_req.arg2), "on");
                break;
            default:
                return ERR_DEVICE_TYPE_MISMATCH;
        }

        rc = timer_propagate_to_children(dev, &auto_req);
        if (rc == OK){

            timer->base.info.state = target_state;
        }
    }

    return OK;
}










int timer_device_main(device_id id) {
    timer_device timer;
    int fd,dummy_fd;
    int rc;

    memset(&timer,0,sizeof(timer));
    rc=device_common_init(&timer.base,id,DEVICE_TIMER) ;
    if(rc!=OK) {
        return rc;
    }

    timer.base.handle_message=timer_handle_message;
    timer.base.destroy=timer_destroy;

    timer.base.update = timer_update;

    rc=timer_init(&timer.base) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_setup_fifo(&timer.base) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_open_fifo(&timer.base,&fd,&dummy_fd) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_main_loop(&timer.base,fd) ;
    device_common_cleanup(&timer.base,fd,dummy_fd);
    return rc;
}
