#include "config.h"
#include "libbb.h"

#ifdef CONFIG_FEATURE_UNCOMPRESS

/* uncompress for busybox -- (c) 2002 Robert Griebl
 *
 * based on the original compress42.c source 
 * (see disclaimer below)
 */


/* (N)compress42.c - File compression ala IEEE Computer, Mar 1992.
 *
 * Authors:
 *   Spencer W. Thomas   (decvax!harpo!utah-cs!utah-gr!thomas)
 *   Jim McKie           (decvax!mcvax!jim)
 *   Steve Davies        (decvax!vax135!petsd!peora!srd)
 *   Ken Turkowski       (decvax!decwrl!turtlevax!ken)
 *   James A. Woods      (decvax!ihnp4!ames!jaw)
 *   Joe Orost           (decvax!vax135!petsd!joe)
 *   Dave Mack           (csu@alembic.acs.com)
 *   Peter Jannesen, Network Communication Systems
 *                       (peter@ncs.nl)
 *
 * marc@suse.de : a small security fix for a buffer overflow
 *
 * [... History snipped ...]
 *
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	IBUFSIZ	2048	/* Defailt input buffer size							*/
#define	OBUFSIZ	2048	/* Default output buffer size							*/

							/* Defines for third byte of header 					*/
#define	MAGIC_1		(char_type)'\037'/* First byte of compressed file				*/
#define	MAGIC_2		(char_type)'\235'/* Second byte of compressed file				*/
#define BIT_MASK	0x1f			/* Mask for 'number of compresssion bits'		*/
									/* Masks 0x20 and 0x40 are free.  				*/
									/* I think 0x20 should mean that there is		*/
									/* a fourth header byte (for expansion).    	*/
#define BLOCK_MODE	0x80			/* Block compresssion if table is full and		*/
									/* compression rate is dropping flush tables	*/

			/* the next two codes should not be changed lightly, as they must not	*/
			/* lie within the contiguous general code space.						*/
#define FIRST	257					/* first free entry 							*/
#define	CLEAR	256					/* table clear output code 						*/

#define INIT_BITS 9			/* initial number of bits/code */


/*
 * machine variants which require cc -Dmachine:  pdp11, z8000, DOS
 */
#define FAST

#define	HBITS		17			/* 50% occupancy */
#define	HSIZE	   (1<<HBITS)
#define	HMASK	   (HSIZE-1)
#define	HPRIME		 9941
#define	BITS		   16
#undef	MAXSEG_64K

typedef long int			code_int;

typedef long int	 		count_int;
typedef long int			cmp_code_int;

typedef	unsigned char	char_type;

#define MAXCODE(n)	(1L << (n))



int				block_mode = BLOCK_MODE;/* Block compress mode -C compatible with 2.0*/
int				maxbits = BITS;		/* user settable max # bits/code 				*/
int				exit_code = -1;		/* Exitcode of compress (-1 no file compressed)	*/

char_type		inbuf[IBUFSIZ+64];	/* Input buffer									*/
char_type		outbuf[OBUFSIZ+2048];/* Output buffer								*/


count_int		htab[HSIZE];
unsigned short	codetab[HSIZE];

#define	htabof(i)				htab[i]
#define	codetabof(i)			codetab[i]
#define	tab_prefixof(i)			codetabof(i)
#define	tab_suffixof(i)			((char_type *)(htab))[i]
#define	de_stack				((char_type *)&(htab[HSIZE-1]))
#define	clear_htab()			memset(htab, -1, sizeof(htab))
#define	clear_tab_prefixof()	memset(codetab, 0, 256);


/*
 * Decompress stdin to stdout.  This routine adapts to the codes in the
 * file building the "string" table on-the-fly; requiring no table to
 * be stored in the compressed file.  The tables used herein are shared
 * with those of the compress() routine.  See the definitions above.
 */

extern int uncompress(int fd_in, int fd_out)
{
    char_type 		*stackp;
    code_int		 code;
	int				 finchar;
	code_int		 oldcode;
	code_int		 incode;
	int				 inbits;
	int				 posbits;
	int				 outpos;
	int				 insize;
	int				 bitmask;
	code_int		 free_ent;
	code_int		 maxcode;
	code_int		 maxmaxcode;
	int				 n_bits;
	int				 rsize = 0;

	insize = 0;

	inbuf [0] = xread_char(fd_in);

	maxbits = inbuf[0] & BIT_MASK;
	block_mode = inbuf[0] & BLOCK_MODE;
	maxmaxcode = MAXCODE(maxbits);

	if (maxbits > BITS)
	{
		fprintf(stderr,	"compressed with %d bits, can only handle %d bits\n", maxbits, BITS);
		return -1;
	}

	//fprintf(stderr, "Bits: %d, block_mode: %d\n", maxbits, block_mode );

    maxcode = MAXCODE(n_bits = INIT_BITS)-1;
	bitmask = (1<<n_bits)-1;
	oldcode = -1;
	finchar = 0;
	outpos = 0;
	posbits = 0<<3;

    free_ent = ((block_mode) ? FIRST : 256);

	clear_tab_prefixof();	/* As above, initialize the first
							   256 entries in the table. */

    for (code = 255 ; code >= 0 ; --code)
		tab_suffixof(code) = (char_type)code;

	do
	{
resetbuf:	;
		{
			int	i;
			int	e;
			int	o;

			e = insize-(o = (posbits>>3));

			for (i = 0 ; i < e ; ++i)
				inbuf[i] = inbuf[i+o];

			insize = e;
			posbits = 0;
		}

		if (insize < (int) sizeof(inbuf)-IBUFSIZ)
		{
			xread_all(fd_in, inbuf+insize, IBUFSIZ);
			insize += rsize;
		}

		inbits = ((rsize > 0) ? (insize - insize%n_bits)<<3 : 
								(insize<<3)-(n_bits-1));

		while (inbits > posbits)
		{
			if (free_ent > maxcode)
			{
				posbits = ((posbits-1) + ((n_bits<<3) -
								 (posbits-1+(n_bits<<3))%(n_bits<<3)));

				++n_bits;
				if (n_bits == maxbits)
					maxcode = maxmaxcode;
				else
				    maxcode = MAXCODE(n_bits)-1;

				bitmask = (1<<n_bits)-1;
				goto resetbuf;
			}


			{	
				char_type *p = &inbuf[posbits>>3];
				code = ((((long)(p[0]))|((long)(p[1])<<8)|((long)(p[2])<<16))>>(posbits&0x7))&bitmask;
			}
			posbits += n_bits;
			

			if (oldcode == -1)
			{
				outbuf[outpos++] = (char_type)(finchar = (int)(oldcode = code));
				continue;
			}

			if (code == CLEAR && block_mode)
			{
				clear_tab_prefixof();
    			free_ent = FIRST - 1;
				posbits = ((posbits-1) + ((n_bits<<3) -
							(posbits-1+(n_bits<<3))%(n_bits<<3)));
			    maxcode = MAXCODE(n_bits = INIT_BITS)-1;
				bitmask = (1<<n_bits)-1;
				goto resetbuf;
			}

			incode = code;
		    stackp = de_stack;

			if (code >= free_ent)	/* Special case for KwKwK string.	*/
			{
				if (code > free_ent)
				{
					char_type 		*p;

					posbits -= n_bits;
					p = &inbuf[posbits>>3];

					fprintf(stderr, "insize:%d posbits:%d inbuf:%02X %02X %02X %02X %02X (%d)\n", insize, posbits,
							p[-1],p[0],p[1],p[2],p[3], (posbits&07));
		    		fprintf(stderr, "uncompress: corrupt input\n");
					return -1;
				}

    	    	*--stackp = (char_type)finchar;
	    		code = oldcode;
			}

			while ((cmp_code_int)code >= (cmp_code_int)256)
			{ /* Generate output characters in reverse order */
		    	*--stackp = tab_suffixof(code);
		    	code = tab_prefixof(code);
			}

			*--stackp =	(char_type)(finchar = tab_suffixof(code));

		/* And put them out in forward order */

			{
				int	i;

				if (outpos+(i = (de_stack-stackp)) >= OBUFSIZ)
				{
					do
					{
						if (i > OBUFSIZ-outpos) i = OBUFSIZ-outpos;

						if (i > 0)
						{
							memcpy(outbuf+outpos, stackp, i);
							outpos += i;
						}

						if (outpos >= OBUFSIZ)
						{
							write(fd_out, outbuf, outpos);
							outpos = 0;
						}
						stackp+= i;
					}
					while ((i = (de_stack-stackp)) > 0);
				}
				else
				{
					memcpy(outbuf+outpos, stackp, i);
					outpos += i;
				}
			}

			if ((code = free_ent) < maxmaxcode) /* Generate the new entry. */
			{
		    	tab_prefixof(code) = (unsigned short)oldcode;
		    	tab_suffixof(code) = (char_type)finchar;
    			free_ent = code+1;
			} 

			oldcode = incode;	/* Remember previous code.	*/
		}

    }
	while (rsize > 0);

	if (outpos > 0) {
		write(fd_out, outbuf, outpos);
	}

	return 0;
}


#endif
