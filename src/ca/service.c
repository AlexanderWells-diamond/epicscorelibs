/************************************************************************/
/*									*/
/*	        	      L O S  A L A M O S			*/
/*		        Los Alamos National Laboratory			*/
/*		         Los Alamos, New Mexico 87545			*/
/*									*/
/*	Copyright, 1986, The Regents of the University of California.	*/
/*									*/
/*									*/
/*	History								*/
/*	-------								*/
/*									*/
/*	Date	Person	Comments					*/
/*	----	------	--------					*/
/*	8/87	joh	Init Release					*/
/*	1/90	joh	Made cmmd for errors which			*/
/*					eliminated the status field	*/
/*	030791	joh	Fixed problem where pend count was decremented	*/
/*			prior to connecting chan (setting the type).	*/
/*	041591	joh	added call to channel exsits routine		*/
/*	060491  joh	fixed structure pass for SPARC cc		*/
/*	060691	joh	reworked to remove 4 byte count before each	*/
/*			macro message					*/
/*	071291	joh	now claiming channel in use block over TCP so	*/
/*			problems with duplicate port assigned to	*/
/*			client after reboot go away			*/
/*	072391	joh	added event locking for vxWorks			*/
/*    	100391  joh     added missing ntohs() for the VAX               */
/*	111991	joh	converted MACRO in iocinf.h to cac_io_done()	*/
/*			here						*/
/*	022692	joh	moved modify of chan->state inside lock to	*/
/*			provide sync necissary for new use of this 	*/
/*			field.						*/
/*	031692	joh	When bad cmd in message detected disconnect	*/
/*			instead of terminating the client		*/
/*	041692	joh	set state to cs_conn before caling 		*/
/* 			ca_request_event() so their channel 		*/
/*			connect tests wont fail				*/
/*	042892	joh	No longer checking the status from free() 	*/
/*			since it varies from os to os			*/
/*	040592	joh	took out extra cac_send_msg() calls		*/
/*	072792	joh	better messages					*/
/*	110592	joh	removed ptr dereference from test for valid	*/
/*			monitor handler function			*/
/*	111792	joh	reset retry delay for cast iiu when host	*/
/*			indicates the channel's address has changed	*/
/*	120992	joh	converted to dll list routines			*/
/*	122192	joh	now decrements the outstanding ack count	*/
/*									*/
/*_begin								*/
/************************************************************************/
/*									*/
/*	Title:	channel access service routines				*/
/*	File:	atcs:[ca]service.c					*/
/*	Environment: VMS, UNIX, VRTX					*/
/*	Equipment: VAX, SUN, VME					*/
/*									*/
/*									*/
/*	Purpose								*/
/*	-------								*/
/*									*/
/*	channel access service routines					*/
/*									*/
/*									*/
/*	Special comments						*/
/*	------- --------						*/
/*									*/
/************************************************************************/
/*_end									*/

static char *sccsId = "$Id$";

#include 	"iocinf.h"
#include 	"net_convert.h"


LOCAL void reconnect_channel(
IIU			*piiu,
chid			chan
);

LOCAL int cacMsg(
struct ioc_in_use 	*piiu,
struct in_addr  	*pnet_addr
);

LOCAL void perform_claim_channel(
IIU			*piiu,
struct in_addr          *pnet_addr
);

#ifdef CONVERSION_REQUIRED 
globalref CACVRTFUNC *cac_dbr_cvrt[];
#endif /*CONVERSION_REQUIRED*/


/*
 * post_msg()
 * 
 * 
 * LOCK should be applied when calling this routine
 * 
 */
int post_msg(
struct ioc_in_use 	*piiu,
struct in_addr  	*pnet_addr,
char			*pInBuf,
unsigned long		blockSize
)
{
	int		status;
	unsigned long 	size;

	while (blockSize) {

		/*
		 * fetch a complete message header
		 */
		if(piiu->curMsgBytes < sizeof(piiu->curMsg)){
			char  *pHdr;

			size = sizeof(piiu->curMsg) - piiu->curMsgBytes;
			size = min(size, blockSize);
			
			pHdr = (char *) &piiu->curMsg;
			memcpy(	pHdr + piiu->curMsgBytes, 
				pInBuf, 
				size);
			
			piiu->curMsgBytes += size;
			if(piiu->curMsgBytes < sizeof(piiu->curMsg)){
				return OK;
			}

			pInBuf += size;
			blockSize -= size;
		
			/* 
			 * fix endian of bytes 
			 */
			piiu->curMsg.m_postsize = 
				ntohs(piiu->curMsg.m_postsize);
			piiu->curMsg.m_cmmd = ntohs(piiu->curMsg.m_cmmd);
			piiu->curMsg.m_type = ntohs(piiu->curMsg.m_type);
			piiu->curMsg.m_count = ntohs(piiu->curMsg.m_count);
#if 0
			ca_printf("%s Cmd=%3d Type=%3d Count=%4d Size=%4d",
				piiu->host_name_str,
				piiu->curMsg.m_cmmd,
				piiu->curMsg.m_type,
				piiu->curMsg.m_count,
				piiu->curMsg.m_postsize);
			ca_printf(" Avail=%8x Cid=%6d\n",
				piiu->curMsg.m_available,
				piiu->curMsg.m_cid);
#endif
		}


		/*
		 * dont allow huge msg body until
		 * the server supports it
		 */
		if(piiu->curMsg.m_postsize>(unsigned)MAX_TCP){
			piiu->curMsgBytes = 0;
			piiu->curDataBytes = 0;
			return ERROR;
		}

		/*
		 * make sure we have a large enough message body cache
		 */
		if(piiu->curMsg.m_postsize>piiu->curDataMax){
			if(piiu->pCurData){
				free(piiu->pCurData);
			}
			piiu->curDataMax = 0;
			piiu->pCurData = (void *) 
				malloc(piiu->curMsg.m_postsize);
			if(!piiu->pCurData){
				piiu->curMsgBytes = 0;
				piiu->curDataBytes = 0;
				return ERROR;
			}
			piiu->curDataMax = 
				piiu->curMsg.m_postsize;
		}

		/*
 		 * Fetch a complete message body
		 */
		if(piiu->curMsg.m_postsize>piiu->curDataBytes){
			char *pBdy;

			size = piiu->curMsg.m_postsize - piiu->curDataBytes; 
			size = min(size, blockSize);
			pBdy = piiu->pCurData;
			memcpy(	pBdy+piiu->curDataBytes, 
				pInBuf, 
				size);
			piiu->curDataBytes += size;
			if(piiu->curDataBytes < piiu->curMsg.m_postsize){
				return OK;
			}
			pInBuf += size;
			blockSize -= size;
		}	


		/*
 		 * execute the response message
		 */
		status = cacMsg(piiu, pnet_addr);
		piiu->curMsgBytes = 0;
		piiu->curDataBytes = 0;
		if(status != OK){
			return ERROR;
		}
	}
	return OK;
}


/*
 * cacMsg()
 */
LOCAL int cacMsg(
struct ioc_in_use 	*piiu,
struct in_addr  	*pnet_addr
)
{
	evid            monix;
	int             status;

	switch (piiu->curMsg.m_cmmd) {

	case IOC_NOOP:
		break;

	case IOC_ECHO:
		piiu->echoPending = FALSE;
		break;

	case IOC_WRITE_NOTIFY:
	{
		/*
		 * run the user's event handler
		 * m_available points to event descriptor
		 */
		struct event_handler_args args;

		monix = (evid) piiu->curMsg.m_available;

		/*
		 * 
		 * call handler, only if they did not clear the
		 * chid in the interim
		 */
		if (monix->usr_func) {
			args.usr = monix->usr_arg;
			args.chid = monix->chan;
			args.type = monix->type;
			args.count = monix->count;
			args.dbr = NULL;
			/*
			 * the channel id field is abused for
			 * write notify status
			 *
			 * write notify was added vo CA V4.1
			 */
			args.status = ntohl(piiu->curMsg.m_cid); 

			LOCKEVENTS;
			(*monix->usr_func) (args);
			UNLOCKEVENTS;
		}
		LOCK;
		ellDelete(&pend_write_list, &monix->node);
		ellAdd(&free_event_list, &monix->node);
		UNLOCK;

		break;

	}	
	case IOC_READ_NOTIFY:
	{
		/*
		 * run the user's event handler
		 * m_available points to event descriptor
		 */
		struct event_handler_args args;

		monix = (evid) piiu->curMsg.m_available;

		/*
		 * 
		 * call handler, only if they did not clear the
	 	 * chid in the interim
		 */
		if (monix->usr_func) {
			int v41;

			/*
			 * convert the data buffer from net
			 * format to host format
			 *
			 */
#			ifdef CONVERSION_REQUIRED 
				(*cac_dbr_cvrt[piiu->curMsg.m_type])(
					piiu->pCurData, 
					piiu->pCurData, 
					FALSE,
					piiu->curMsg.m_count);
#			endif

			args.usr = monix->usr_arg;
			args.chid = monix->chan;
			args.type = piiu->curMsg.m_type;
			args.count = piiu->curMsg.m_count;
			args.dbr = piiu->pCurData;
			/*
			 * the channel id field is abused for
			 * read notify status starting
			 * with CA V4.1
			 */
			v41 = CA_V41(
				CA_PROTOCOL_VERSION, 
				piiu->minor_version_number);
			if(v41){
				args.status = ntohl(piiu->curMsg.m_cid);
			}
			else{
				args.status = ECA_NORMAL;
			}

			LOCKEVENTS;
			(*monix->usr_func) (args);
			UNLOCKEVENTS;
		}
		LOCK;
		ellDelete(&pend_read_list, &monix->node);
		ellAdd(&free_event_list, &monix->node);
		UNLOCK;

		break;
	}
	case IOC_EVENT_ADD:
	{
		int v41;
		struct event_handler_args args;

		/*
		 * run the user's event handler m_available
		 * points to event descriptor
		 */
		monix = (evid) piiu->curMsg.m_available;


		/*
		 * m_postsize = 0  is a confirmation of a
		 * monitor cancel
		 */
		if (!piiu->curMsg.m_postsize) {
			LOCK;
			ellDelete(&monix->chan->eventq, &monix->node);
			ellAdd(&free_event_list, &monix->node);
			UNLOCK;

			break;
		}
		/* only call if not disabled */
		if (!monix->usr_func)
			break;

		/*
		 * convert the data buffer from net
		 * format to host format
		 */
#		ifdef CONVERSION_REQUIRED 
			(*cac_dbr_cvrt[piiu->curMsg.m_type])(
				piiu->pCurData, 
				piiu->pCurData, 
				FALSE,
				piiu->curMsg.m_count);
#		endif

		/*
		 * Orig version of CA didnt use this
		 * strucure. This would run faster if I had
		 * decided to pass a pointer to this
		 * structure rather than the structure itself
		 * early on.
		 */
		args.usr = monix->usr_arg;
		args.chid = monix->chan;
		args.type = piiu->curMsg.m_type;
		args.count = piiu->curMsg.m_count;
		args.dbr = piiu->pCurData;
		/*
		 * the channel id field is abused for
		 * event status starting
		 * with CA V4.1
		 */
		v41 = CA_V41(
			CA_PROTOCOL_VERSION, 
			piiu->minor_version_number);
		if(v41){
			args.status = ntohl(piiu->curMsg.m_cid); 
		}
		else{
			args.status = ECA_NORMAL;
		}

		/* call their handler */
		LOCKEVENTS;
		(*monix->usr_func) (args);
		UNLOCKEVENTS;

		break;
	}
	case IOC_READ:
	{
		chid            chan;

		/*
		 * verify the channel id
		 */
		LOCK;
		chan = bucketLookupItem(pBucket, piiu->curMsg.m_cid);
		UNLOCK;
		if(!chan){
			ca_signal(ECA_INTERNAL, 
				"bad client channel id from server");
			break;
		}

		/*
		 * only count get returns if from the current
		 * read seq
		 */
		if (!VALID_MSG(piiu))
			break;

		/*
		 * convert the data buffer from net
		 * format to host format
		 */
#		ifdef CONVERSION_REQUIRED 
			(*cac_dbr_cvrt[piiu->curMsg.m_type])(
				piiu->pCurData, 
				piiu->curMsg.m_available, 
				FALSE,
				piiu->curMsg.m_count);
#		else
			memcpy(
				(char *)piiu->curMsg.m_available,
				piiu->pCurData,
				dbr_size_n(piiu->curMsg.m_type, piiu->curMsg.m_count));
#		endif

		/*
		 * decrement the outstanding IO count
		 */
		CLRPENDRECV(TRUE);
		break;
	}
	case IOC_SEARCH:
		perform_claim_channel(piiu, pnet_addr);
		break;

	case IOC_READ_SYNC:
		piiu->read_seq++;
		break;

	case IOC_RSRV_IS_UP:
		LOCK;
		{
			struct in_addr ina;
			
			ina.s_addr = (long) piiu->curMsg.m_available;
			mark_server_available(&ina);
		}
		UNLOCK;
		break;

	case REPEATER_CONFIRM:
		ca_static->ca_repeater_contacted = TRUE;
#ifdef DEBUG
		ca_printf("CAC: repeater confirmation recv\n");
#endif
		break;

	case IOC_NOT_FOUND:
		break;

	case IOC_CLEAR_CHANNEL:
	{
		chid            	chix = (chid) piiu->curMsg.m_available;
		struct ioc_in_use 	*piiu = chix->piiu;
		register evid   	monix;


		LOCK;
		/*
		 * remove any orphaned get callbacks for this
		 * channel
		 */
		for (monix = (evid) pend_read_list.node.next;
		     monix;
		     monix = (evid) monix->node.next){
			if (monix->chan == chix) {
				ellDelete(	
					&pend_read_list, 
					&monix->node);
				ellAdd(
					&free_event_list, 
					&monix->node);
			}
		}
		ellConcat(&free_event_list, &chix->eventq);
		ellDelete(&piiu->chidlist, &chix->node);
		status = bucketRemoveItem(pBucket, chix->cid, chix);
		if(status != BUCKET_SUCCESS){
			ca_signal(
				ECA_INTERNAL,
				"bad id at channel delete");
		}
		free(chix);
		if (!piiu->chidlist.count){
			TAG_CONN_DOWN(piiu);
		}
		UNLOCK;
		break;
	}
	case IOC_ERROR:
	{
		char		nameBuf[64];
		char           	context[255];
		struct extmsg  	*req = piiu->pCurData;
		int             op;
		struct exception_handler_args args;


		/*
		 * dont process the message if they have
		 * disabled notification
		 */
		if (!ca_static->ca_exception_func){
			break;
		}

		caHostFromInetAddr(pnet_addr, nameBuf, sizeof(nameBuf));

		if (piiu->curMsg.m_postsize > sizeof(struct extmsg)){
			sprintf(context, 
				"detected by: %s for: %s", 
				nameBuf, 
				(char *)(req+1));
		}
		else{
			sprintf(context, "detected by: %s", nameBuf);
		}

		/*
		 * Map internal op id to external op id so I
		 * can freely change the protocol in the
		 * future. This is quite wasteful of space
		 * however.
		 */
		switch (ntohs(req->m_cmmd)) {
		case IOC_READ_NOTIFY:
		case IOC_READ:
			op = CA_OP_GET;
			break;
		case IOC_WRITE_NOTIFY:
		case IOC_WRITE:
			op = CA_OP_PUT;
			break;
		case IOC_SEARCH:
			op = CA_OP_SEARCH;
			break;
		case IOC_EVENT_ADD:
			op = CA_OP_ADD_EVENT;
			break;
		case IOC_EVENT_CANCEL:
			op = CA_OP_CLEAR_EVENT;
			break;
		default:
			op = CA_OP_OTHER;
			break;
		}

		LOCK;
		args.chid = bucketLookupItem(pBucket, piiu->curMsg.m_cid);
		UNLOCK;
		args.usr = ca_static->ca_exception_arg;
		args.type = ntohs(req->m_type);	
		args.count = ntohs(req->m_count);
		args.addr = (void *) (req->m_available);
		args.stat = ntohl((long) piiu->curMsg.m_available);							args.op = op;
		args.ctx = context;

		LOCKEVENTS;
		(*ca_static->ca_exception_func) (args);
		UNLOCKEVENTS;
		break;
	}
	case IOC_ACCESS_RIGHTS:
	{
		int	ar;
		chid	chan;

		LOCK;
		chan = bucketLookupItem(pBucket, piiu->curMsg.m_cid);
		UNLOCK;
		assert(chan);

		ar = ntohl(piiu->curMsg.m_available);
		chan->ar.read_access = (ar&CA_ACCESS_RIGHT_READ)?1:0;
		chan->ar.write_access = (ar&CA_ACCESS_RIGHT_WRITE)?1:0;

		if (chan->pAccessRightsFunc) {
			struct access_rights_handler_args 	args;

			args.chid = chan;
			args.ar = chan->ar;
			(*chan->pAccessRightsFunc)(args);
		}
		break;
	}
	case IOC_CLAIM_CIU:
	{
		chid	chan;

		LOCK;
		chan = bucketLookupItem(pBucket, piiu->curMsg.m_cid);
		UNLOCK;
		assert(chan);
		reconnect_channel(piiu, chan);
		break;
	}
	default:
		ca_printf("CAC: post_msg(): Corrupt cmd in msg %x\n", 
			piiu->curMsg.m_cmmd);

		return ERROR;
	}

	return OK;
}


/*
 *
 *	perform_claim_channel()
 *
 */
LOCAL void perform_claim_channel(
IIU			*piiu,
struct in_addr          *pnet_addr
)
{
	int		v42;
	char		rej[64];
      	chid		chan;
	int		status;
	IIU		*allocpiiu;
	IIU		*chpiiu;
	unsigned short	*pMinorVersion;

	/*
	 * ignore broadcast replies for deleted channels
	 * 
	 * lock required around use of the sprintf buffer
	 */
	LOCK;
	chan = bucketLookupItem(pBucket, piiu->curMsg.m_available);
	if(!chan){
		UNLOCK;
		return;
	}

	chpiiu = chan->piiu;
	if(!chpiiu){
		ca_printf("cast reply to local channel??\n");
		UNLOCK;
		return;
	}

	/*
	 * Ignore search replies to closing channels 
	 */ 
	if(chan->state == cs_closed) {
		UNLOCK;
		return;
	}

	/*
	 * Ignore duplicate search replies
	 */
	if (chpiiu != piiuCast) {
		caAddrNode	*pNode;

		pNode = (caAddrNode *) chpiiu->destAddr.node.next;
		assert(pNode);
		if (pNode->destAddr.inetAddr.sin_addr.s_addr != 
				pnet_addr->s_addr) {

			caHostFromInetAddr(pnet_addr,rej,sizeof(rej));
			sprintf(
				sprintf_buf,
		"Channel: %s Accepted: %s Rejected: %s ",
				(char *)(chan + 1),
				chpiiu->host_name_str,
				rej);
			ca_signal(ECA_DBLCHNL, sprintf_buf);
		}
		UNLOCK;
		return;
	}

        status = alloc_ioc(pnet_addr, &allocpiiu);
	switch(status){

	case ECA_NORMAL:
		break;

	case ECA_DISCONN:
		/*
		 * This indicates that the connection is tagged
		 * is tagged for shutdown and we are waiting for 
		 * it to go away. Search replies are ignored
		 * in the interim.
		 */
		UNLOCK;
	  	return;

	default:
		caHostFromInetAddr(pnet_addr,rej,sizeof(rej));
	  	ca_printf("CAC: ... %s ...\n", ca_message(status));
	 	ca_printf("CAC: for %s on %s\n", chan+1, rej);
	 	ca_printf("CAC: ignored search reply- proceeding\n");
		UNLOCK;
	  	return;

	}

	/*
	 * Starting with CA V4.1 the minor version number
	 * is appended to the end of each search reply.
	 * This value is ignored by earlier clients.
	 */
	if(piiu->curMsg.m_postsize >= sizeof(*pMinorVersion)){
		pMinorVersion = (unsigned short *)(piiu->pCurData);
        	allocpiiu->minor_version_number = ntohs(*pMinorVersion);      
	}
	else{
		allocpiiu->minor_version_number = CA_UKN_MINOR_VERSION;
	}

        ellDelete(&chpiiu->chidlist, &chan->node);
	chan->piiu = allocpiiu;
        ellAdd(&allocpiiu->chidlist, &chan->node);

	/*
	 * If this is the first channel to be
	 * added to this IIU then communicate
	 * the client's name to the server. 
	 * (CA V4.1 or higher)
	 */
	if(ellCount(&allocpiiu->chidlist)==1){
		issue_identify_client(allocpiiu);
		issue_client_host_name(allocpiiu);
	}

	/*
	 * claim the resource in the IOC
	 * over TCP so problems with duplicate UDP port
	 * after reboot go away
	 */
        chan->id.sid = piiu->curMsg.m_cid;
	issue_claim_channel(allocpiiu, chan);

	/*
	 * Assume that we have access once connected briefly
	 * until the server is able to tell us the correct
	 * state for backwards compatibility.
	 *
	 * Their access rights call back does not get
	 * called for the first time until the information 
	 * arrives however.
	 */
	chan->ar.read_access = TRUE;
	chan->ar.write_access = TRUE;

	UNLOCK;

	v42 = CA_V42(
		CA_PROTOCOL_VERSION, 
		allocpiiu->minor_version_number);
	if(!v42){
		reconnect_channel(piiu, chan);
	}
}


/*
 * reconnect_channel()
 */
LOCAL void reconnect_channel(
IIU		*piiu,
chid		chan
)
{
      	evid			pevent;
	enum channel_state	prev_cs;
	int			v41;

	LOCK;

	v41 = CA_V41(
			CA_PROTOCOL_VERSION, 
			((IIU *)chan->piiu)->minor_version_number);

        /*	Update rmt chid fields from extmsg fields	*/
        chan->type  = piiu->curMsg.m_type;      
        chan->count = piiu->curMsg.m_count;      


	/*
	 * set state to cs_conn before caling 
	 * ca_request_event() so their channel 
	 * connect tests wont fail
 	 */
	prev_cs = chan->state;
	chan->state = cs_conn;

	/*
	 * NOTE: monitor and callback reissue must occur prior to calling
	 * their connection routine otherwise they could be requested twice.
	 */
#ifdef CALLBACK_REISSUE
        /* reissue any outstanding get callbacks for this channel */
	if(pend_read_list.count){
          	for(	pevent = (evid) pend_read_list.node.next; 
			pevent; 
			pevent = (evid) pevent->node.next){
            		if(pevent->chan == chan){
	      			issue_get_callback(pevent);
	    		}
		}
        }
#endif

        /* reissue any events (monitors) for this channel */
	if(chan->eventq.count){
          	for( 	pevent = (evid)chan->eventq.node.next; 
			pevent; 
			pevent = (evid)pevent->node.next)
	    		ca_request_event(pevent);
 	}

      	UNLOCK;

	/*
	 * if less than v4.1 then the server will never
	 * send access rights and we know that there
	 * will always be access and call their call back
	 * here
	 */
	if (chan->pAccessRightsFunc && !v41) {
		struct access_rights_handler_args 	args;

		args.chid = chan;
		args.ar = chan->ar;
		(*chan->pAccessRightsFunc)(args);
	}

      	if(chan->pConnFunc){
		struct connection_handler_args	args;

		args.chid = chan;
		args.op = CA_OP_CONN_UP;
		LOCKEVENTS;
        	(*chan->pConnFunc)(args);
		UNLOCKEVENTS;
	}
	else if(prev_cs==cs_never_conn){
          	/* decrement the outstanding IO count */
          	CLRPENDRECV(TRUE);
	}


	UNLOCK;
}


/*
 *
 *	cas_io_done()
 *
 *
 */
void cac_io_done(int lock)
{
  	struct pending_io_event	*pioe;
	
  	if(ioeventlist.count==0)
		return;

	if(lock){
 		LOCK;
	}

    	while(pioe = (struct pending_io_event *) ellGet(&ioeventlist)){
      		(*pioe->io_done_sub)(pioe->io_done_arg);
      		free(pioe);
	}

	if(lock){
  		UNLOCK;
	}
}
