/*
 *  Interpress utilities
 *
 *  Written for Xerox Corporation by William LeFebvre
 *  24-May-1984
 *
 * Copyright (c) 1984, 1985 Xerox Corp.
 *
 * HISTORY
 * 15-Jan-86  lee at Xerox, WRC
 *	Removed the rest of the Vax dependencies.
 *
 *	10-sep-85  lee moore	Removed some dependencies on the Vax.
 *				Plenty more to be removed
 *	29-apr-85  ed flint	add conditional compilation for vax-11 c (vms)
 */

/*
 *  Subroutines to help build interpress files:
 *
 *  literal interface level - these routines produce interpress output at
 *			      the token level.
 */

/*
 *  NOTE:  Some of these "subroutines" are one-liners, so they are written
 *	   as macros for better efficiency.
 */

# define Rational_max	1073741824
# define ip_Buffsize	1024

#ifndef vax11c
# include <sys/param.h>
# include <math.h>
# include <stdio.h>
#endif

# include "iptokens.h"
# include "literal.h"		/* macro definitions for some routines */

# ifndef NULL
# define NULL (char *)0
# endif

#ifdef vax11c
# define NOFILE 20
#endif

# define   No	0
# define   Yes	1

static int  ip_fd = -1;			/* current Interpress file */
static char ip_filebuff[ip_Buffsize];	/* file buffer */
static char *ip_buffptr = NULL;		/* points in to ip_filebuff */
static int  ip_bytecnt;			/* number of bytes in ip_filebuff */
static char ip_files[NOFILE] = { 0 };	/* marks which files are ip files */

/*
 *  Definitions for the primitives suggested in the Interpress standard
 *  (XSIS 048302).  The following primitives are defined with macros in
 *  "literal.h":
 *
 *	AppendComment
 *	AppendIdentifier
 *	AppendInsertFile
 *	AppendString
 *
 *  Currently, AppendString will only handle byte values -- it will not
 *  try to insert any escape characters.  The rationale for this is that
 *  ASCII (which is what this system uses) doesn't have character codes that
 *  high.
 */

AppendOp(opcode)

int opcode;

{
    if (opcode > SHORT_OP_LIMIT)
    {
	/* has to be coded as a long op */
	append_word((unsigned short)((LONG_OP << 8) | opcode));
    }
    else
    {
	/* small enough to be a short op */
	append_byte((unsigned char)(SHORT_OP | opcode));
    }
}

AppendNumber(number)

double number;

{
    long d;
    double r;
    
    if (number == (double)(d = (long)number))
    {
	AppendInteger(d);
    }
    else
    {
	d = 1;
	while ((fabs(r = number * d) < Rational_max) &&
	       (d < Rational_max) &&
	       (r != (float)((int)(r))))
	{
	    d <<= 1;
	}
	AppendRational((long)r, d);
    }
}


/*
 * note that although the routine is called AppendInteger, it is really
 * AppendLong.  This is because alot of code wants to use 32 bit numbers.
 * If you want to pass it a 16bit int, that will work too.
 */

AppendInteger(number)

long number;

{
    if (number < INTEGER_MIN || number > INTEGER_MAX)
    {
	append_integer_sequence(number);
    }
    else
    {
	append_short_number((short) number);
    }
}

AppendRational(value, divisor)

long value, divisor;

{
    int len_value, len_divisor, len;

    len_value = bytes_in_int(value);
    len_divisor = bytes_in_int(divisor);

    len = len_value > len_divisor ? len_value : len_divisor;
    append_Sequence(sequenceRational, len << 1, (unsigned char *)NULL);
    append_n_byte_int(value, len);
    append_n_byte_int(divisor, len);
}

#ifdef notdef
AppendIntegerVector(vector, num)

int *vector;	/* ??? */
int  number;

{
    
}
#endif

/*
 *  The remainder of this file contains lower level primitives:
 */

/*
 *  append_Sequence(type, length, buff)
 *
 *  Append a sequence descriptor and its data bytes.  The descriptor is of
 *  type "type" and length "length".  "Buff" points to the buffer containing
 *  the data.  If "length" is negative, then bytes from "buffer" are written
 *  back to front (this makes writing integers easy).
 */

append_Sequence(type, length, buff)

int  type;
int  length;
unsigned char *buff;

{
# ifdef notnow
    /* some day, we should make this check, but not today */
    if ((length & 0x7f000000) != 0)
    {
	/* too big to fit in a long ... */
    }
# endif

    /* check for swapped byte correction */
    if (length < 0)
    {
	fprintf(stderr, "negative sequence!\n");
	abort();
    }

    if ((length & 0x7fffff00) != 0)
    {
	/* too big to fit in a short sequence */
	append_byte((unsigned char) (LONG_SEQUENCE | type));
	append_n_byte_int((long) length, 3);
    }
    else
    {
	append_byte((unsigned char) (SHORT_SEQUENCE | type));
	append_byte((unsigned char) length);
    }

    /* tack on data, if any */
    if (buff != NULL)
        append_bytes(length, buff);
}


/*
 * append_integer_sequence(number)
 *	A special version of append_sequence that handles integers.  Integers
 *	must be treated differently because the natural representation of an
 *	integer for a particular machine maybe byte swapped relative to the
 *	Xerox standard.
 */
append_integer_sequence(number)

long number;

{
    int length = bytes_in_int(number);

    append_byte((unsigned char) (SHORT_SEQUENCE | sequenceInteger));
    append_byte((unsigned char) length);
    append_n_byte_int(number, length);
}

/*
 * append_n_byte_int(number, length)
 *	Append N bytes of an integer to the interpress master.
 */

append_n_byte_int(number, length)

long number;
int length;	/* measured in bytes */

{
    int shift;

#ifdef notdef
    switch( length ) {
	case 4:
		append_byte((unsigned char) (number >> 24));
	case 3:
		append_byte((unsigned char) (number >> 16));
	case 2:
		append_byte((unsigned char) (number >>  8));
	case 1:
		append_byte((unsigned char) number);
		break;
	default:
		fprintf(stderr, "append_n_byte_int: asked to append %d bytes\n", length);
	}
#else
    if( length > sizeof(long) )
	fprintf(stderr, "append_n_byte_int: asked to append %d bytes\n", length);

    for( shift = (length - 1)*8; shift >= 0; shift -= 8 )
	append_byte((unsigned char) (number >> shift));
#endif
}
	

/*
 *  append_word(value) - write the two byte (word) value "value"
 */

append_word(value)

unsigned short value;

{
#ifdef notdef
    append_n_byte_int(value, 2);
#else
    append_byte((unsigned char) (value >> 8));
    append_byte((unsigned char) value);
#endif
}

/*
 *  append_byte(value) - write out a byte
 */

append_byte(value)

unsigned char value;

{
    *ip_buffptr++ = value;
    if (++ip_bytecnt == ip_Buffsize)
    {
	write(ip_fd, ip_filebuff, ip_Buffsize);
	ip_bytecnt = 0;
	ip_buffptr = ip_filebuff;
    }
}

/*
 *  append_bytes(length, buff) - write the buffer of bytes pointed to by
 *				 "buff" with length "length".
 */

append_bytes(length, buff)

int length;
unsigned char *buff;

{
    while (length-- > 0)
    {
	append_byte(*buff++);
    }
}


/* this routine assumes 4 bytes in an int and two's complement notation */
/* this routine should be replaced! -lee */
/* this routine sometime over estimates the size of an integer. why? */

bytes_in_int(value)

long value;

{
    int i;
    long mask;

    if (value < 0)
    {
	/* takes the same space as its one's complemented value */
	value = ~value;
    }
    if (value == 0)
    {
	/* avoids infinite looping */
	return(1);
    }
    for (i = 4, mask = 0xff800000; (value & mask) == 0; i--, mask >>= 8)
	;
    return(i);
}

/*
 *  ip_select(fd) - select file descriptor "fd" as the Interpress file for
 *		    later use by the i/o routines supplied in this library.
 */

ip_select(fd)

int fd;

{
    if (ip_fd != -1)
    {
	ip_flush();
    }

    /* set our idea of current file descriptor and initialize the buffer */
    ip_fd = fd;
    ip_buffptr = ip_filebuff;
    ip_bytecnt = 0;

    /* check for initialization */
    if (!ip_files[fd])
    {
	/* not an Intepress file -- initialize it */
	append_bytes(strlen(IP_Header), (unsigned char *) IP_Header);
	ip_files[fd] = Yes;
    }
}

/*
 *  ip_raw_select(fd) - same as ip_select, but no header is placed at the
 *			front of the file.
 */

ip_raw_select(fd)

int fd;

{
    /* trick ip_select into thinking that it is already initialized */
    ip_files[fd] = Yes;

    /* do a normal select */
    ip_select(fd);
}

ip_close()

{
    if (ip_fd != -1)
    {
	ip_flush();
	ip_files[ip_fd] = No;
	close(ip_fd);
	ip_fd = -1;
    }
}

ip_flush()

{
    /* flush the buffer */
    if (ip_Buffsize - ip_bytecnt > 0)
    {
	write(ip_fd, ip_filebuff, ip_bytecnt);
    }
}
