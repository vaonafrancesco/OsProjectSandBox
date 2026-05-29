#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "utils.h"
#include "routing.h"

typedef struct {
    device base;
    bool manual_override;
} hub_device;

static const char *state_str(state state) 
{
    switch(state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

static int hub_propagate_to_children(device *dev,const domo_message *req) {
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

    // send msg to all children
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

static int hub_check_children_consistency(device *dev,bool *consistent_out) 
{
    device_id children[MAX_DEVICES];
    int child_count=0;
    int rc;
    int i;
    state first_state=STATE_OFF ;
    bool first_set=false ;

    if(dev==NULL || consistent_out==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    rc=routing_collect_children(dev->info.id,children,MAX_DEVICES,&child_count) ;
    if(rc!=OK) {
        return rc;
    }

    if(child_count==0) {
        *consistent_out=true ;
        return OK;
    }

    for(i=0;i<child_count;i++) {
        domo_message req;
        domo_message resp;
        char child_fifo[PATH_MAX];
        char reply_fifo[PATH_MAX];

        memset(&req,0,sizeof(req));
        req.kind=MSG_REQUEST ;
        snprintf(req.sender_id,sizeof(req.sender_id),"%d",dev->info.id) ;
        snprintf(req.command,sizeof(req.command),"%s",CMD_INFO);
        req.src_id=dev->info.id ;
        req.dst_id=children[ i ];
        req.target_id=children[ i ] ;
        req.src_pid=getpid();
        req.request_id=(int)getpid() ;
        snprintf(req.payload,sizeof(req.payload),"ALL") ;

        if(make_device_fifo_path(children[ i ],child_fifo,sizeof(child_fifo))!=OK) {
            continue;
        }

        if(make_reply_fifo_path(getpid(),req.request_id,reply_fifo,sizeof(reply_fifo))!=OK) {
            continue;
        }

        rc=request_reply(child_fifo,reply_fifo,&req,&resp) ;
        if(rc!=OK) {
            continue;
        }

        if( !first_set ){
            first_state=dev->info.state ;
            first_set=true;
        } else {
            if(dev->info.state!=first_state) {
                *consistent_out=false ;
                return OK;
            }
        }
    }

    *consistent_out=true ;
    return OK;
}

static int hub_build_info_payload(hub_device *hub,char *buf,size_t len) {
    bool consistent;
    int rc;

    if(hub==NULL || buf==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    rc=hub_check_children_consistency(&hub->base,&consistent) ;
    if(rc!=OK) {
        snprintf(buf,len,"hub id=%d state=%s error=consistency_check_failed",
                 hub->base.info.id,state_str(hub->base.info.state));
        return OK;
    }

    if(hub->manual_override || !consistent) {
        snprintf(buf,len,"hub id=%d state=manual_override",
                 hub->base.info.id);
    } else {
        snprintf(buf,len,"hub id=%d state=%s",
                 hub->base.info.id,state_str(hub->base.info.state)) ;
    }

    return OK;
}

static int hub_handle_message(device *dev,const domo_message *req,domo_message *resp) 
{
    hub_device *hub=(hub_device *)dev;

    if(hub==NULL || req==NULL || resp==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    memset(resp,0,sizeof(*resp));
    resp->kind=MSG_RESPONSE ;
    snprintf(resp->command,sizeof(resp->command),"%s",req->command);
    snprintf(resp->sender_id,sizeof(resp->sender_id),"%d",hub->base.info.id) ;
    resp->src_id=hub->base.info.id;
    resp->dst_id=req->src_id ;
    resp->src_pid=getpid();
    resp->request_id=req->request_id ;
    resp->status=OK;

    simulate_random_delay();

    if(strcmp(req->command,CMD_INFO)==0) {
        return hub_build_info_payload(hub,resp->payload,sizeof(resp->payload));
    }

    if(strcmp(req->command,CMD_SWITCH)==0) {
        int rc;

        rc=hub_propagate_to_children(dev,req) ;
        if(rc!=OK) {
            resp->status=rc ;
            snprintf(resp->payload,sizeof(resp->payload),"failed to propagate switch to children");
            return OK;
        }

        hub->manual_override=false ;

        if(strcmp(req->arg2,"on")==0) {
            hub->base.info.state=STATE_ON ;
        } else if( strcmp(req->arg2,"off")==0 ){
            hub->base.info.state=STATE_OFF;
        } else {
            resp->status=ERR_INVALID_PARAMETERS;
            snprintf(resp->payload,sizeof(resp->payload),"invalid switch position");
            return OK;
        }

        snprintf(resp->payload,sizeof(resp->payload),
                 "hub %d switched %s",hub->base.info.id,state_str(hub->base.info.state)) ;
        return OK;
    }

    if(strcmp(req->command,CMD_STATUS)==0) {
        if(strncmp(req->payload,"manual_override,",16)==0) {
            hub->manual_override=true ;
        }
        return OK;
    }

    resp->status=ERR_INVALID_COMMAND ;
    snprintf(resp->payload,sizeof(resp->payload),"unknown command");
    return OK;
}

static int hub_init(device *dev) {
    hub_device *hub=(hub_device *)dev;

    if(hub==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    hub->manual_override=false;
    hub->base.info.state=STATE_OFF ;

    return OK;
}

static int hub_destroy(device *dev) {
    hub_device *hub=(hub_device *)dev;

    if(hub==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    return OK;
}

int hub_device_main(device_id id) {
    hub_device hub;
    int fd,dummy_fd;
    int rc;

    memset(&hub,0,sizeof(hub));
    rc=device_common_init(&hub.base,id,DEVICE_HUB) ;
    if(rc!=OK) {
        return rc;
    }

    hub.base.handle_message=hub_handle_message;
    hub.base.destroy=hub_destroy;
    rc=hub_init(&hub.base) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_setup_fifo(&hub.base) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_open_fifo(&hub.base,&fd,&dummy_fd) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_main_loop(&hub.base,fd) ;
    device_common_cleanup(&hub.base,fd,dummy_fd);
    return rc;
}
