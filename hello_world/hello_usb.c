/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"

#define DBUG 1

struct PTRs {
	/*This is the buffer for inp & output
	2048 x 2048 = 4194304
	256 x 256 = 65536
	*/
	short int inpbuf[4096*2];
	short int *inp_buf;
	short int *out_buf;
	short int flag;
	short int w;
	short int h;
	short int *fwd_inv;
	short int fwd;
	short int *red;
} ptrs;

void	singlelift(short int rb, short int w, short int * const ibuf, short int * const obuf) {
	short int	col, row;
	//printf("in singlelift \n");
	for(row=0; row<w; row++) {
		register short int	*ip, *op, *opb;
		register short int	ap,b,cp,d;

		//
		// Ibuf walks down rows (here), but reads across columns (below)
		// We might manage to get rid of the multiply by doing something
		// like: 
		//	ip = ip + (rb-w);
		// but we'll keep it this way for now.
		//
		//setting to beginning of each row
		ip = ibuf+row*rb;

		//
		// Obuf walks across columns (here), writing down rows (below)
		//
		// Here again, we might be able to get rid of the multiply,
		// but let's get some confidence as written first.
		//
		op = obuf+row;
		opb = op + w*rb/2;
		//printf("ip = 0x%x op = 0x%x opb = 0x%x\n",ip,op,opb);
		//
		// Pre-charge our pipeline
		//

		// a,b,c,d,e ...
		// Evens get updated first, via the highpass filter
		ap = ip[0];
		b  = ip[1];
		cp = ip[2];
		d  = ip[3]; ip += 4;
		//printf("ap = %d b = %d cp = %d d = %d\n",ap,b,cp,d);
		//
		ap = ap-b; // img[0]-(img[1]+img[-1])>>1)
		cp = cp- ((b+d)>>1);
		 
		op[0] = ap;
		opb[0]  = b+((ap+cp+2)>>2);

		for(col=1; col<w/2-1; col++) {
			op +=rb; // = obuf+row+rb*col = obuf[col][row]
			opb+=rb;// = obuf+row+rb*(col+w/2) = obuf[col+w/2][row]
			ap = cp;
			b  = d;
			cp = ip[0];	// = ip[row][2*col+1]
			d  = ip[1];	// = ip[row][2*col+2]

			//HP filter in fwd dwt			
			cp  = (cp-((b+d)>>1)); //op[0] is obuf[col][row]
			*op = ap; //op[0] is obuf[col][row]
			 
			//LP filter in fwd dwt
			*opb = b+((ap+cp+2)>>2);
			ip+=2;	// = ibuf + (row*rb)+2*col
		}

		op += rb; opb += rb;
		*op  = cp;
		*opb = d+((cp+1)>>3);
	}
}

void	ilift(short int rb, short int w,  short int * const ibuf,  short int * const obuf) {
	short int	col, row;

	for(row=0; row<w; row++) {
		register short int	*ip, *ipb, *op;
		register short int	b,c,d,e;

		//
		// Ibuf walks down rows (here), but reads across columns (below)
		// We might manage to get rid of the multiply by doing something
		// like: 
		//	ip = ip + (rb-w);
		// but we'll keep it this way for now.
		//
		//setting to beginning of each row
		op = obuf+row*rb;

		//
		// Obuf walks across columns (here), writing down rows (below)
		//
		// Here again, we might be able to get rid of the multiply,
		// but let's get some confidence as written first.
		//
		ip  = ibuf+row;
		ipb = ip + w*rb/2;
		//printf("ip = 0x%x op = 0x%x ipb = 0x%x\n",ip,op,ipb);
		//
		// Pre-charge our pipeline
		//

		// a,b,c,d,e ...
		// Evens get updated first, via the highpass filter
		c = ip[0]; // would've been called 'a'
		ip += rb;
		e = ip[0];	// Would've been called 'c'
		d  = ipb[0] -((c+e+2)>>2);

		op[0] = c+d;	// Here's the mirror, left-side
		op[1] = d;
		//printf("c = %d e = %d d = %d c+d = %d\n",c,e,c,c+d);
		for(col=1; col<w/2-1; col++) {
			op += 2;
			ip += rb; ipb += rb;

			c = e; b = d;
			e = ip[0];
			d = ipb[0] - ((c+e+2)>>2);
			c = c + ((b+d)>>1);

			op[0] = c;
			op[1] = d;
		}

		ipb += rb;
		d = ipb[0] - ((e+1)>>3);

		c = e + ((b+d)>>1);
		op[2] = c;
		op[3] = d;	// Mirror
	}
}

void	lifting(short int w, short int *ibuf, short int *tmpbuf, short int *fwd) {
	const	short int	rb = w;
	short int	lvl;

	short int	*ip = ibuf, *tp = tmpbuf, *test_fwd = fwd;
	//printf("ip = 0x%x tp = 0x%x \n",ip,tp);
	short int	ov[3];

	const short int	LVLS = 3;

/*
	for(lvl=0; lvl<w*w; lvl++)
		ibuf[lvl] = 0;
	for(lvl=0; lvl<w*w; lvl++)
		tmpbuf[lvl] = 5000;

	for(lvl=0; lvl<w; lvl++)
		ibuf[lvl*(rb+1)] = 20;

	singlelift(rb,w,ip,tp);
	for(lvl=0; lvl<w*w; lvl++)
		ibuf[lvl] = tmpbuf[lvl];

	return;
*/

	for(lvl=0; lvl<LVLS; lvl++) {
		// Process columns -- leave result in tmpbuf
		//printf("in lifting \n");
		singlelift(rb, w, ip, tp);
		// Process columns, what used to be the rows from the last
		// round, pulling the data from tmpbuf and moving it back
		// to ibuf.
		//printf("w = 0x%x ip = 0x%x tp = 0x%x \n",w,ip,tp);
		singlelift(rb, w, tp, ip);
		//printf("back from singlelift\n");
		// lower_upper
		//
		// For this, we just adjust our pointer(s) so that the "image"
		// we are processing, as referenced by our two pointers, now
		// references the bottom right part of the image.
		//
		// Of course, we haven't really changed the dimensions of the
		// image.  It started out rb * rb in size, or the initial w*w,
		// we're just changing where our pointer into the image is.
		// Rows remain rb long.  We pretend (above) that this new image
		// is w*w, or should I say (w/2)*(w/2), but really we're just
		// picking a new starting coordinate and it remains rb*rb.
		//
		// Still, this makes a subimage, within our image, containing
		// the low order results of our processing.
		short int	offset = w*rb/2+w/2;
		ip = &ip[offset];
		tp = &tp[offset];
		ov[lvl] = offset + ((lvl)?(ov[lvl-1]):0);

		// Move to the corner, and repeat
		w>>=1;
	}
	//printf("testing test_fwd \n");
	if (test_fwd[0]==0) {
	for(lvl=(LVLS-1); lvl>=0; lvl--) {
		short int	offset;

		w <<= 1;

		if (lvl)
			offset = ov[lvl-1];
		else
			offset = 0;
		ip = &ibuf[offset];
		tp = &tmpbuf[offset];
		//printf("ip = 0x%x tp = 0x%x \n",ip,tp);

		ilift(rb, w, ip, tp);
		//printf("back from ilift\n");
		ilift(rb, w, tp, ip);
		//printf("back from ilift\n");
	}
	}
}

const char src[] = "Hello, world! ";
const short int a[] = {161,159,157,155,160,170,168,134, 97,106,109,109,109,
    157,157,156,155,163,170,164,131, 95,104,108,108,108
    };

int main() {
    stdio_init_all();
    ptrs.w = 64;
    ptrs.h = 64;
    
    ptrs.inp_buf = ptrs.inpbuf; 
	ptrs.out_buf = ptrs.inpbuf + 4096;
	
	ptrs.fwd_inv =  &ptrs.fwd;
    *ptrs.fwd_inv = 1;
    
    
    while (true) {
        if (DBUG == 1 ) {
            printf("Hello, world!\n");
            printf("Now copmpiles with lifting code as part of hello_usb.c\n"); 
            printf("structure PTRS added to hello_usb.c\n");
            printf("ptrs.w = %d ptrs.h = %d \n", ptrs.w, ptrs.h);
            printf("These are the variables needed for lifting\n");
            printf("ptrs.inp_buf = 0x%x ptrs.out_buf = 0x%x\n",ptrs.inp_buf, ptrs.out_buf);
            
            printf("w = %d ptrs.fwd_inv = 0x%x ptrs.fwd_inv = %d\n",ptrs.w,ptrs.fwd_inv, *ptrs.fwd_inv); 
            for(int i=0;i<25;i++) printf("%d ",a[i]);
            printf("\n");
        } 
        sleep_ms(1000);
    }
    return 0;
}
