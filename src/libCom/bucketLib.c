/*
 *      Author: Jeffrey O. Hill
 *              hill@atdiv.lanl.gov
 *              (505) 665 1831
 *      Date:  	9-93 
 *
 *      Experimental Physics and Industrial Control System (EPICS)
 *
 *      Copyright 1991, the Regents of the University of California,
 *      and the University of Chicago Board of Governors.
 *
 *      This software was produced under  U.S. Government contracts:
 *      (W-7405-ENG-36) at the Los Alamos National Laboratory,
 *      and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *      Initial development by:
 *              The Controls and Automation Group (AT-8)
 *              Ground Test Accelerator
 *              Accelerator Technology Division
 *              Los Alamos National Laboratory
 *
 *      Co-developed with
 *              The Controls and Computing Group
 *              Accelerator Systems Division
 *              Advanced Photon Source
 *              Argonne National Laboratory
 *
 *      Modification Log:
 *      -----------------
 */


#ifdef vxWorks
#include <stdioLib.h>
#else /* vxWorks */
#include <stdio.h>
#endif /* vxWorks */

#include <stdlib.h>

#include <bucketLib.h>

#ifndef NULL
#define NULL 0
#endif /* NULL */
#ifndef NBBY
#define NBBY 8
#endif /* NBBY */

#define BUCKET_IX_WIDTH		12
#define BUCKET_IX_N		(1<<BUCKET_IX_WIDTH)
#define BUCKET_IX_MASK		(BUCKET_IX_N-1)	

typedef union itemPtr{
	void	*pItem;
	BUCKET	*pBucket;
}ITEMPTR;

#ifdef DEBUG
main()
{
	BUCKETID	id;
	int		s;
	BUCKET		*pb;
	char		*pValSave;
	char		*pVal;
	unsigned	i;

	pb = bucketCreate(NBBY*sizeof(BUCKETID));
	if(!pb){
		return BUCKET_FAILURE;
	}

	id = 444;
	pValSave = "fred";
	s = bucketAddItem(pb, id, pValSave);
	if(s != BUCKET_SUCCESS){
		return BUCKET_FAILURE;
	}

	printf("Begin\n");
	for(i=0; i<500000; i++){
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
		pVal = bucketLookupItem(pb, id);
		if(pVal != pValSave){
			printf("failure\n");
			break;
		}
	}
	printf("End\n");

	return BUCKET_SUCCESS;
}
#endif


/*
 * bucketCreate()
 */
BUCKET	*bucketCreate(unsigned indexWidth)
{
	BUCKET		*pb;
	unsigned	nentries;

	if(indexWidth>sizeof(BUCKETID)*NBBY){
		return NULL;
	}

	pb = (BUCKET *) calloc(1, sizeof(*pb));
	if(!pb){
		return pb;
	}

	if(indexWidth>BUCKET_IX_WIDTH){
		pb->indexShift = indexWidth - BUCKET_IX_WIDTH;
	}
	else{
		pb->indexShift = 0;
	}
	pb->nextIndexMask = (1<<pb->indexShift)-1;
	nentries = 1<<(indexWidth-pb->indexShift);
	pb->indexMask = nentries-1; 

	pb->pTable = (ITEMPTR *) calloc(
			nentries, 
			sizeof(ITEMPTR));
	if(!pb->pTable){
		return NULL;
	}
	return pb;
}



/*
 * bucketAddItem()
 */
int	bucketAddItem(BUCKET *prb, BUCKETID id, void *pItem)
{
	ITEMPTR	*pi;

	/*
	 * is the id to big ?
	 */
	if(id&~prb->indexMask){
		return BUCKET_FAILURE;
	}

	if(prb->indexShift){
		BUCKET	*pb;

		pi = &prb->pTable[id>>prb->indexShift];
		pb = pi->pBucket;

		if(!pb){
			pb = bucketCreate(prb->indexShift);
			if(!pb){
				return BUCKET_FAILURE;
			}
			pi->pBucket = pb;
			prb->nInUse++;
		}	

		return bucketAddItem(
			pb, 
			id&prb->nextIndexMask, 
			pItem);
	}
	
	pi = &prb->pTable[id];
	if(pi->pItem){
		return BUCKET_FAILURE;
	}

	pi->pItem = pItem;
	prb->nInUse++;

	return BUCKET_SUCCESS;
}


/*
 * bucketRemoveItem()
 */
int	bucketRemoveItem(BUCKET *prb, BUCKETID id, void *pItem)
{
	ITEMPTR	*ppi;

	/*
	 * is the id to big ?
	 */
	if(id&~prb->indexMask){
		return BUCKET_FAILURE;
	}

	if(prb->indexShift){
		BUCKET	*pb;
		int	s;

		ppi = &prb->pTable[id>>prb->indexShift];
		pb = ppi->pBucket;

		if(!pb){
			return BUCKET_FAILURE;
		}	

		s = bucketRemoveItem(
			pb, 
			id&prb->nextIndexMask, 
			pItem);
		if(s!=BUCKET_SUCCESS){
			return s;
		}

		if(pb->nInUse==0){
			free(pb->pTable);
			free(pb);
			ppi->pBucket = NULL;
			prb->nInUse--;
		}
		return s;
	}

	ppi = &prb->pTable[id];
	if(ppi->pItem != pItem){
		return BUCKET_FAILURE;
	}

	prb->nInUse--;
	ppi->pItem = NULL;

	return BUCKET_SUCCESS;
}


/*
 * bucketLookupItem()
 */
void	*bucketLookupItem(BUCKET *pb, BUCKETID id)
{
	unsigned shift;

	/*
	 * is the id to big ?
	 */
	if(id&~pb->indexMask){
		return NULL;
	}

	while(shift = pb->indexShift){
		BUCKETID nextId;

		nextId = id & pb->nextIndexMask;

		pb = pb->pTable[id>>shift].pBucket;
		if(!pb){
			return pb;
		}	
		id = nextId;	
	}

	return pb->pTable[id].pItem;
}



/*
 * bucketShow()
 */
int	bucketShow(BUCKET *pb)
{
	ITEMPTR	*pi;

	pi = pb->pTable;

	printf(	"Bucket: mask=%x entries in use=%d bytes in use=%d\n",
		pb->indexMask<<pb->indexShift,
		pb->nInUse,
		sizeof(*pb)+(pb->indexMask+1)*sizeof(*pi));

	if(pb->indexShift){
		for(	pi = pb->pTable;
			pi<=&pb->pTable[pb->indexMask];
			pi++){
			if(pi->pBucket){
				bucketShow(pi->pBucket);
			}
		}
	}
	return BUCKET_SUCCESS;
}


