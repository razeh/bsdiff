/*-
 * Copyright 2003-2005 Colin Percival
 * Copyright 2012 Matthew Endsley
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bsdiff.h"

#include <limits.h>
#include <string.h>
#include "divsufsort.h"

#define MIN(x,y) (((x)<(y)) ? (x) : (y))

static int64_t matchlen(const uint8_t *old,int64_t oldsize,const uint8_t *new,int64_t newsize)
{
        int64_t i;

        for(i=0;(i<oldsize)&&(i<newsize);i++)
                if(old[i]!=new[i]) break;

        return i;
}

static int64_t search(saidx_t *I,const uint8_t *old,int64_t oldsize,
                const uint8_t *new,int64_t newsize,int64_t st,int64_t en,int64_t *pos)
{
        int64_t x,y;

        if(en-st<2) {
                x=matchlen(old+I[st],oldsize-I[st],new,newsize);
                y=matchlen(old+I[en],oldsize-I[en],new,newsize);

                if(x>y) {
                        *pos=I[st];
                        return x;
                } else {
                        *pos=I[en];
                        return y;
                }
        };

        x=st+(en-st)/2;
        if(memcmp(old+I[x],new,MIN(oldsize-I[x],newsize))<0) {
                return search(I,old,oldsize,new,newsize,x,en,pos);
        } else {
                return search(I,old,oldsize,new,newsize,st,x,pos);
        };
}

static void offtout(int64_t x,uint8_t *buf)
{
        int64_t y;

        if(x<0) y=-x; else y=x;

        buf[0]=y%256;y-=buf[0];
        y=y/256;buf[1]=y%256;y-=buf[1];
        y=y/256;buf[2]=y%256;y-=buf[2];
        y=y/256;buf[3]=y%256;y-=buf[3];
        y=y/256;buf[4]=y%256;y-=buf[4];
        y=y/256;buf[5]=y%256;y-=buf[5];
        y=y/256;buf[6]=y%256;y-=buf[6];
        y=y/256;buf[7]=y%256;

        if(x<0) buf[7]|=0x80;
}

static int64_t writedata(struct bsdiff_stream* stream, const void* buffer, int64_t length, int type)
{
        int64_t result = 0;

        while (length > 0)
        {
                const int smallsize = (int)MIN(length, INT_MAX);
                const int writeresult = stream->write(stream, buffer, smallsize, type);
                if (writeresult == -1)
                {
                        return -1;
                }

                result += writeresult;
                length -= smallsize;
                buffer = (uint8_t*)buffer + smallsize;
        }

        return result;
}

struct bsdiff_request
{
        const uint8_t* old;
        int64_t oldsize;
        const uint8_t* new;
        int64_t newsize;
        struct bsdiff_stream* stream;
        saidx_t *I;
        uint8_t *buffer;
};

static int bsdiff_internal(const struct bsdiff_request req)
{
        saidx_t *I;
        int64_t scan,pos,len;
        int64_t lastscan,lastpos,lastoffset;
        int64_t oldscore,scsc;
        int64_t s,Sf,lenf,Sb,lenb;
        int64_t overlap,Ss,lens;
        int64_t i;
        uint8_t *buffer;
        uint8_t buf[8 * 3];

        I = req.I;

	if (divsufsort(req.old, I, req.oldsize)) { return -1; }

        buffer = req.buffer;

        /* Compute the differences, writing ctrl as we go */
        scan=0;len=0;pos=0;
        lastscan=0;lastpos=0;lastoffset=0;
        while(scan<req.newsize) {
                oldscore=0;

                for(scsc=scan+=len;scan<req.newsize;scan++) {
                        len=search(I,req.old,req.oldsize,req.new+scan,req.newsize-scan,
                                        0,req.oldsize,&pos);

                        for(;scsc<scan+len;scsc++)
                        if((scsc+lastoffset<req.oldsize) &&
                                (req.old[scsc+lastoffset] == req.new[scsc]))
                                oldscore++;

                        if(((len==oldscore) && (len!=0)) ||
                                (len>oldscore+8)) break;

                        if((scan+lastoffset<req.oldsize) &&
                                (req.old[scan+lastoffset] == req.new[scan]))
                                oldscore--;
                };

                if((len!=oldscore) || (scan==req.newsize)) {
                        s=0;Sf=0;lenf=0;
                        for(i=0;(lastscan+i<scan)&&(lastpos+i<req.oldsize);) {
                                if(req.old[lastpos+i]==req.new[lastscan+i]) s++;
                                i++;
                                if(s*2-i>Sf*2-lenf) { Sf=s; lenf=i; };
                        };

                        lenb=0;
                        if(scan<req.newsize) {
                                s=0;Sb=0;
                                for(i=1;(scan>=lastscan+i)&&(pos>=i);i++) {
                                        if(req.old[pos-i]==req.new[scan-i]) s++;
                                        if(s*2-i>Sb*2-lenb) { Sb=s; lenb=i; };
                                };
                        };

                        if(lastscan+lenf>scan-lenb) {
                                overlap=(lastscan+lenf)-(scan-lenb);
                                s=0;Ss=0;lens=0;
                                for(i=0;i<overlap;i++) {
                                        if(req.new[lastscan+lenf-overlap+i]==
                                           req.old[lastpos+lenf-overlap+i]) s++;
                                        if(req.new[scan-lenb+i]==
                                           req.old[pos-lenb+i]) s--;
                                        if(s>Ss) { Ss=s; lens=i+1; };
                                };

                                lenf+=lens-overlap;
                                lenb-=lens;
                        };

                        offtout(lenf,buf);
                        offtout((scan-lenb)-(lastscan+lenf),buf+8);
                        offtout((pos-lenb)-(lastpos+lenf),buf+16);

                        /* Write control data */
                        if (writedata(req.stream, buf, sizeof(buf), BSDIFF_WRITECONTROL))
                                return -1;

                        /* Write diff data */
                        for(i=0;i<lenf;i++)
                                buffer[i]=req.new[lastscan+i]-req.old[lastpos+i];
                        if (writedata(req.stream, buffer, lenf, BSDIFF_WRITEDIFF))
                                return -1;

                        /* Write extra data */
                        for(i=0;i<(scan-lenb)-(lastscan+lenf);i++)
                                buffer[i]=req.new[lastscan+lenf+i];
                        if (writedata(req.stream, buffer, (scan-lenb)-(lastscan+lenf), BSDIFF_WRITEEXTRA))
                                return -1;

                        lastscan=scan-lenb;
                        lastpos=pos-lenb;
                        lastoffset=pos-scan;
                };
        };

        return 0;
}

int bsdiff(const uint8_t* source, int64_t sourcesize, const uint8_t* target, int64_t targetsize, struct bsdiff_stream* stream)
{
        int result;
        struct bsdiff_request req;

        if((req.I=stream->malloc((sourcesize+1)*sizeof(saidx_t)))==NULL)
                return -1;

        if((req.buffer=stream->malloc(targetsize+1))==NULL)
        {
                stream->free(req.I);
                return -1;
        }

        req.old = source;
        req.oldsize = sourcesize;
        req.new = target;
        req.newsize = targetsize;
        req.stream = stream;

        result = bsdiff_internal(req);

        stream->free(req.buffer);
        stream->free(req.I);

        return result;
}

#if defined(BSDIFF_EXECUTABLE)

#include <sys/types.h>

#include <bzlib.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int bz2_write(struct bsdiff_stream* stream, const void* buffer, int size, int type __attribute__((__unused__)))
{
        int bz2err;
        BZFILE* bz2;

        bz2 = (BZFILE*)stream->opaque;
        BZ2_bzWrite(&bz2err, bz2, (void*)buffer, size);
        if (bz2err != BZ_STREAM_END && bz2err != BZ_OK)
                return -1;

        return 0;
}

int main(int argc,char *argv[])
{
        int fd;
        int bz2err;
        uint8_t *old,*new;
        off_t oldsize,newsize;
        uint8_t buf[8];
        FILE * pf;
        struct bsdiff_stream stream;
        BZFILE* bz2;

        memset(&bz2, 0, sizeof(bz2));
        stream.malloc = malloc;
        stream.free = free;
        stream.write = bz2_write;

        if(argc!=4) errx(1,"usage: %s oldfile newfile patchfile\n",argv[0]);

        /* Allocate oldsize+1 bytes instead of oldsize bytes to ensure
                that we never try to malloc(0) and get a NULL pointer */
        if(((fd=open(argv[1],O_RDONLY,0))<0) ||
                ((oldsize=lseek(fd,0,SEEK_END))==-1) ||
                ((old=malloc(oldsize+1))==NULL) ||
                (lseek(fd,0,SEEK_SET)!=0) ||
                (read(fd,old,oldsize)!=oldsize) ||
                (close(fd)==-1)) err(1,"%s",argv[1]);


        /* Allocate newsize+1 bytes instead of newsize bytes to ensure
                that we never try to malloc(0) and get a NULL pointer */
        if(((fd=open(argv[2],O_RDONLY,0))<0) ||
                ((newsize=lseek(fd,0,SEEK_END))==-1) ||
                ((new=malloc(newsize+1))==NULL) ||
                (lseek(fd,0,SEEK_SET)!=0) ||
                (read(fd,new,newsize)!=newsize) ||
                (close(fd)==-1)) err(1,"%s",argv[2]);

        /* Create the patch file */
        if ((pf = fopen(argv[3], "w")) == NULL)
                err(1, "%s", argv[3]);

        /* Write header (signature+newsize)*/
        offtout(newsize, buf);
        if (fwrite("ENDSLEY/BSDIFF43", 16, 1, pf) != 1 ||
                fwrite(buf, sizeof(buf), 1, pf) != 1)
                err(1, "Failed to write header");


        if (NULL == (bz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)))
                errx(1, "BZ2_bzWriteOpen, bz2err=%d", bz2err);

        stream.opaque = bz2;
        if (bsdiff(old, oldsize, new, newsize, &stream))
                err(1, "bsdiff");

        BZ2_bzWriteClose(&bz2err, bz2, 0, NULL, NULL);
        if (bz2err != BZ_OK)
                err(1, "BZ2_bzWriteClose, bz2err=%d", bz2err);

        if (fclose(pf))
                err(1, "fclose");

        /* Free the memory we used */
        free(old);
        free(new);

        return 0;
}

#endif
