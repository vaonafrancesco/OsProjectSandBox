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
    device_id children[MAX_DEVICES];
    int child_count=0;
    int rc;
    int i;

    if(dev==NULL || req==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    rc=routing_collect_children(dev->info.id,children,MAX_DEVICES,&child_count) ;
    if(rc!=OK) {
        return rc;
    }

    // forward to all children devices
    for(i=0;i<child_count;i++) {
        domo_message child_req;
        char child_fifo[PATH_MAX];

        memset(&child_req,0,sizeof(child_req));
        child_req.kind=MSG_REQUEST ;
        snprintf(child_req.sender_id,sizeof(child_req.sender_id),"%d",dev->info.id);
        snprintf(child_req.command,sizeof(child_req.command),"%s",req->command) ;
        child_req.src_id=dev->info.id;
        child_req.dst_id=children[ i ] ;
        child_req.target_id=children[ i ];
        child_req.src_pid=getpid();
        child_req.request_id=(int)getpid() ;
        snprintf(child_req.arg1,sizeof(child_req.arg1),"%s",req->arg1) ;
        snprintf(child_req.arg2,sizeof(child_req.arg2),"%s",req->arg2);
        snprintf(child_req.payload,sizeof(child_req.payload),"%s",req->payload) ;

        if(make_device_fifo_path(children[ i ],child_fifo,sizeof(child_fifo))!=OK) {
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

static int timer_handle_message(device *dev,const domo_message *req,domo_message *resp) 
{
    timer_device *timer=(timer_device *)dev;

    if(timer==NULL || req==NULL || resp==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

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
        }
        return OK;
    }

    resp->status=ERR_INVALID_COMMAND ;
    snprintf(resp->payload,sizeof(resp->payload),"unknown command");
    return OK;
}

static int timer_init(device *dev) {
    timer_device *timer=(timer_device *)dev;

    if(timer==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    timer->manual_override=false;
    timer->base.info.state=STATE_OFF ;
    strcpy(timer->begin_time,"00:00") ;
    strcpy(timer->end_time,"00:00");

    return OK;
}

static int timer_destroy(device *dev) {
    timer_device *timer=(timer_device *)dev;

    if(timer==NULL) {
        return ERR_INVALID_PARAMETERS ;
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
