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

// Simple helper to print the state
static const char *state_str(state state) 
{
    switch(state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

// Checks if the user typed the time correctly
static int validate_time_format(const char *time_str) {
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

// Compares two "HH:MM" strings.
static int compare_times(const char *time1,const char *time2) {
    int h1,m1,h2,m2;

    sscanf(time1,"%d:%d",&h1,&m1);
    sscanf(time2,"%d:%d",&h2,&m2);

    if(h1<h2) return -1;
    if(h1>h2) return 1;
    if(m1<m2) return -1;
    if(m1>m2) return 1;
    return 0;
}

// If the timer receives a command it needs to pass that command down to whatever device it is controlling.
static int timer_propagate_to_children(device *dev,const domo_message *req) 
{
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

// Builds the string to reply to the "info" command
static int timer_build_info_payload(timer_device *timer,char *buf,size_t len) {
    if(timer==NULL || buf==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    if(timer->manual_override) {
        snprintf(buf,len,"timer id=%d parent=%d state=manual_override begin=%s end=%s",
                 timer->base.info.id,timer->base.info.logical_parent_id, timer->begin_time,timer->end_time);
    } else {
        snprintf(buf,len,"timer id=%d parent=%d state=%s begin=%s end=%s",
                 timer->base.info.id, timer->base.info.logical_parent_id, state_str(timer->base.info.state),
                 timer->begin_time,timer->end_time) ;
    }

    return OK;
}

// Parsing helper
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

// Parsing helper
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

// Handles incoming messages.
static int timer_handle_message(device *dev,const domo_message *req,domo_message *resp) 
{
    timer_device *timer=(timer_device *)dev;

    if(timer==NULL || req==NULL || resp==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }
    
    // Update the clock before processing anything
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
	
	// COMMAND: INFO
    if(strcmp(req->command,CMD_INFO)==0) {
        return timer_build_info_payload(timer,resp->payload,sizeof(resp->payload));
    }
	
	// COMMAND: SWITCH
	/*	If a human manually switches the timer, we propagate it to the child
    	and put the timer in "manual override" mode so the clock stops interfering. */
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
	
	// COMMAND: STATUS
	/*	If a child reports that someone manually changed its state,
    	the timer enters manual override mode. */
    if(strcmp(req->command,CMD_STATUS)==0) {
        if(strncmp(req->payload,"manual_override,",16)==0) {
            timer->manual_override=true ;
            timer->base.info.manual_override = true;
        }
        return OK;
    }
	
	// COMMAND: SET
	// Used to set the begin and end times of the timer
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
	
	// COMMAND: CHILD ADDED
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

		// A timer is designed to only control ONE specific device
        if (timer->base.child_count >= 1) {
            resp->status = ERR_NOT_ALLOWED;
            snprintf(resp->payload, sizeof(resp->payload), "timer can only have one child device");
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
	
	// COMMAND: CHILD REMOVED
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
    
    // COMMAND: LINK
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

// Initial setup for the timer
static int timer_init(device *dev) {
    timer_device *timer = (timer_device *)dev;

    if (timer == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    timer->base.info.logical_parent_id=0;
    timer->manual_override = false;
    timer->base.info.state = STATE_OFF;
    timer->base.info.manual_override = false;
    strcpy(timer->begin_time, "00:00");
    strcpy(timer->end_time, "00:00");
	
	// Dynamic memory allocation: a timer can only hold 1 child
    timer->base.child_capacity = 1;
    timer->base.child_count = 0;
    timer->base.child_ids = malloc(sizeof(device_id));
    if (timer->base.child_ids == NULL) {
        return ERR_SYSTEM;
    }
    timer->base.child_ids[0] = NO_PARENT;

    return OK;
}

// Clean up memory before dying to prevent memory leaks
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

/*	This function runs constantly inside the main select() loop.
	It checks the real-world clock and automatically pushes states to the child. */
static int timer_update(device *dev){
    timer_device *timer = (timer_device *)dev;

    if (timer == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    // If the timer hasn't been set yet, do nothing
    if (strcmp(timer->begin_time, "00:00")== 0 && strcmp(timer->end_time, "00:00")==0){
        return OK;
    }
    // If a human manually changed the child's state, the timer stops interfering
    if (timer->manual_override) {
        return OK;
    }

	// Get the current system time
    time_t now;
    struct tm *tm_now;
    char current_time_str[6];


    time(&now);
    tm_now = localtime(&now);
    snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d",tm_now->tm_hour, tm_now->tm_min);

    bool is_on =false;

	/*	Check if we are inside the ON window.
		We have to handle two cases: normal day or overnight */
    if(compare_times(timer->begin_time, timer->end_time) <0){
    	// Normal case (begin is earlier than end)
        if (compare_times(current_time_str, timer->begin_time) >= 0 && compare_times(current_time_str, timer->end_time)<0){
            is_on = true;
        }
    }else if (compare_times(timer->begin_time, timer->end_time)>0){
    	// Overnight case (begin is later than end, crosses midnight)
        if (compare_times(current_time_str, timer->begin_time)>=0 || compare_times(current_time_str, timer->end_time)<0){
            is_on = true;
        }
    }

    state target_state = is_on ? STATE_ON : STATE_OFF;
	
	// If the clock says the state should change, AND we actually have a child to control
    if (timer->base.info.state != target_state && timer->base.child_count > 0){
        domo_message auto_req;
        memset(&auto_req, 0, sizeof(auto_req));

        snprintf(auto_req.command, sizeof(auto_req.command), "%s", CMD_SWITCH);
      	 // Use the universal label so it works no matter who is the child
        snprintf(auto_req.arg1, sizeof(auto_req.arg1), "sys_state"); 
        snprintf(auto_req.arg2, sizeof(auto_req.arg2), is_on ? "on" : "off");

        // Just one message sent. Zero overhead.
        int rc = timer_propagate_to_children(dev, &auto_req);
        
        if (rc == OK){
            timer->base.info.state = target_state;
        }
    }

    return OK;
}

// emtry
int timer_device_main(device_id id) {
    timer_device timer;
    int fd,dummy_fd;
    int rc;

    memset(&timer,0,sizeof(timer));
    
    // Set up base device variables
    rc=device_common_init(&timer.base,id,DEVICE_TIMER) ;
    if(rc!=OK) {
        return rc;
    }

	// Bind custom functions
    timer.base.handle_message=timer_handle_message;
    timer.base.destroy=timer_destroy;
    timer.base.update = timer_update;

	// Run custom init (allocates memory for child array)
    rc=timer_init(&timer.base) ;
    if(rc!=OK) {
        return rc;
    }
    
	// Setup FIFO file
    rc=device_common_setup_fifo(&timer.base) ;
    if(rc!=OK) {
        return rc;
    }
	
	// Open FIFO for reading
    rc=device_common_open_fifo(&timer.base,&fd,&dummy_fd) ;
    if(rc!=OK) {
        return rc;
    }
	
	// Enter infinite loop (which will constantly call timer_update)
    rc=device_common_main_loop(&timer.base,fd) ;
    // Cleanup on exit
    device_common_cleanup(&timer.base,fd,dummy_fd);
    return rc;
}
