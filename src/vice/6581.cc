/*
 * 6581.cc - MOS6581 (SID) emulation.
 *
 * Note: This is an experimental C64 only version of sid.c, using
 * the reSID library to emulate MOS6581.
 *
 * Written by
 *  Teemu Rantanen (tvr@cs.hut.fi)
 *  Michael Schwendt (sidplay@geocities.com)
 *
 * AIX support added by
 *  Chris Sharp (sharpc@hursley.ibm.com)
 *
 * reSID integration by
 *  Dag Lem (resid@nimrod.no)
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "resid/sid.h"

extern "C" {

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <math.h>

#if defined(HAVE_SYS_IOCTL_H) && !defined(__MSDOS__)
#ifdef __hpux
#define _INCLUDE_HPUX_SOURCE
#endif
#include <sys/ioctl.h>
#endif

#ifndef HAVE_USLEEP
extern int usleep(unsigned long);
#endif

#include "vmachine.h"
#include "types.h"
#include "mem.h"
#include "ui.h"
#include "interrupt.h"
#include "warn.h"
#include "sid.h"
#include "vsync.h"
#include "resources.h"
#include "utils.h"

#ifdef SID
#undef SID
#endif

/* needed datatypes */
typedef unsigned int u32_t;
typedef int s32_t;
typedef unsigned short u16_t;
typedef short s16_t;
typedef unsigned char u8_t;


static SID sid;

/* needed data for SID */
typedef struct sound_s
{
    /* pointer to the sample data that is generated when SID runs */
    s16_t		*pbuf;
    /* offset of next sample being generated */
    s32_t		 bufptr;

    /* internal constant used for sample rate dependent calculations */
    u32_t		 speed1;

    /* warnings */
    warn_t		*pwarn;

    /* do we have a new sid or an old one? */
    BYTE		 newsid;
} sound_t;


/* SID initialization routine */
static void init_sid(sound_t *psid, s16_t *pbuf, int speed)
{
    u32_t		 i;
    char		*name;

    psid->speed1 = (CYCLES_PER_SEC << 8) / speed;

    psid->pbuf = pbuf;
    psid->bufptr = 0;

    name = "SID";

    if (!psid->pwarn)
	psid->pwarn = warn_init(name, 32);
    else
	warn_reset(psid->pwarn);

    sid.enable_filter(app_resources.sidFilters);
    psid->newsid = app_resources.sidModel;
}



/*
 * devices
 */
typedef struct
{
    /* name of the device or NULL */
    char			 *name;
    /* init -routine to be called at device initialization. Should use
       suggested values if possible or return new values if they cannot be
       used */
    int				(*init)(sound_t *s, char *device, int *speed,
					int *fragsize, int *fragnr,
					double bufsize);
    /* send number of bytes to the soundcard. it is assumed to block if kernel
       buffer is full */
    int				(*write)(sound_t *s, s16_t *pbuf, int nr);
    /* dump-routine to be called for every write to SID */
    int				(*dump)(ADDRESS addr, BYTE byte, CLOCK clks);
    /* flush-routine to be called every frame */
    int				(*flush)(sound_t *s);
    /* return number of samples unplayed in the kernel buffer at the moment */
    int				(*bufferstatus)(sound_t *s, int first);
    /* close and cleanup device */
    void			(*close)();
    /* suspend device */
    int				(*suspend)(sound_t *s);
    /* resume device */
    int				(*resume)(sound_t *s);
} sid_device_t;

#define FRAGS_PER_SECOND ((int)RFSH_PER_SEC)

/*
 * fs-device
 */
static FILE *fs_fd = NULL;

static int fs_init(sound_t *s, char *param, int *speed,
		   int *fragsize, int *fragnr, double bufsize)
{
    if (!param)
	param = "vicesnd.raw";
    fs_fd = fopen(param, "w");
    if (!fs_fd)
	return 1;
    return 0;
}

static int fs_write(sound_t *s, s16_t *pbuf, int nr)
{
    int			i;
    i = fwrite(pbuf, sizeof(s16_t), nr, fs_fd);
    if (i != nr)
	return 1;
    return 0;
}

static void fs_close()
{
    fclose(fs_fd);
    fs_fd = NULL;
}

static sid_device_t fs_device =
{
    "fs",
    fs_init,
    fs_write,
    NULL,
    NULL,
    NULL,
    fs_close,
    NULL,
    NULL
};


/*
 * dummy device to get all the benefits of running sid
 */
static sid_device_t dummy_device =
{
    "dummy",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

/*
 * Another dummy device to measure speed (this calculates samples)
 */
static int speed_write(sound_t *s, s16_t *pbuf, int nr)
{
    return 0;
}

static sid_device_t speed_device =
{
    "speed",
    NULL,
    speed_write,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


/*
 * dump device to dump all writes to file for further examination
 */
static FILE *dump_fd = NULL;

static int dump_init(sound_t *s, char *param, int *speed,
		     int *fragsize, int *fragnr, double bufsize)
{
    if (!param)
	param = "vicesnd.sid";
    dump_fd = fopen(param, "w");
    if (!dump_fd)
	return 1;
    return 0;
}

static int dump_dump(ADDRESS addr, BYTE byte, CLOCK clks)
{
    int				i;
    i = fprintf(dump_fd, "%d %d %d\n", (int)clks, addr, byte);
    if (i < 0)
	return 1;
    return 0;
}

static int dump_flush(sound_t *s)
{
    int				i;
    i = fflush(dump_fd);
    return i;
}

static void dump_close()
{
    fclose(dump_fd);
    dump_fd = NULL;
}

static sid_device_t dump_device =
{
    "dump",
    dump_init,
    NULL,
    dump_dump,
    dump_flush,
    NULL,
    dump_close,
    NULL,
    NULL
};


/*
 * timer device to emulate fragmented blocking device behaviour
 */

#ifdef TESTDEVICE

static long test_time_zero = 0;
static long test_time_fragment = 0;
static int test_time_written = 0;
static int test_time_fragsize = 0;
static int test_time_nrfrags = 0;

static long test_now()
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec*1000000 + tp.tv_usec;
}

static int test_bufferstatus(sound_t *s, int first);


static int test_init(sound_t *s, char *param, int *speed,
		     int *fragsize, int *fragnr, double bufsize)
{
    test_time_zero = test_now();
    test_time_fragment = 1000000.0 / ((double)*speed / *fragsize);
    test_time_written = 0;
    test_time_fragsize = *fragsize;
    test_time_nrfrags = *fragnr;
    return 0;
}

static int test_write(sound_t *s, s16_t *pbuf, int nr)
{
    (void)test_bufferstatus(s, 0);
    test_time_written += nr / test_time_fragsize;
    while (test_bufferstatus(s, 0) > test_time_nrfrags*test_time_fragsize)
	usleep(1000000 / (4 * (int)RFSH_PER_SEC));
    return 0;
}

static int test_bufferstatus(sound_t *s, int first)
{
    int			ret;
    long		now = test_now();
    ret = test_time_written - (now - test_time_zero) / test_time_fragment;
    if (ret < 0)
    {
	warn(s->pwarn, -1, "virtual soundbuffer empty");
	test_time_zero = now;
	test_time_written = 0;
	return 0;
    }
    return ret*test_time_fragsize;
}

static sid_device_t test_device =
{
    "test",
    test_init,
    test_write,
    NULL,
    NULL,
    test_bufferstatus,
    NULL,
    NULL,
    NULL
};
#else /* TESTDEVICE */
static sid_device_t test_device;
#endif

/*
 * linux/freebsd -device
 */
#if defined(HAVE_LINUX_SOUNDCARD_H) || defined(HAVE_MACHINE_SOUNDCARD_H)
#if defined(HAVE_LINUX_SOUNDCARD_H)
#include <linux/soundcard.h>
#endif
#if defined(HAVE_MACHINE_SOUNDCARD_H)
#include <machine/soundcard.h>
#endif

static int uss_fd = -1;
static int uss_8bit = 0;
static int uss_bufsize = 0;
static int uss_fragsize = 0;

static int uss_bufferstatus(sound_t *s, int first);

static int uss_init(sound_t *s, char *param, int *speed,
		    int *fragsize, int *fragnr, double bufsize)
{
    int			 st, tmp, orig;
    if (!param)
	param = "/dev/dsp";
    uss_fd = open(param, O_WRONLY, 0777);
    if (uss_fd < 0)
    {
	warn(s->pwarn, -1, "cannot open '%s' for writing", param);
	return 1;
    }
    /* samplesize 16 bits */
#ifdef WORDS_BIGENDIAN
    orig = tmp = AFMT_S16_BE;
#else
    orig = tmp = AFMT_S16_LE;
#endif
    st = ioctl(uss_fd, SNDCTL_DSP_SETFMT, &tmp);
    if (st < 0 || orig != tmp || getenv("USS8BIT"))
    {
	/* samplesize 8 bits */
	orig = tmp = AFMT_U8;
	st = ioctl(uss_fd, SNDCTL_DSP_SETFMT, &tmp);
	if (st < 0 || orig != tmp)
	{
	    warn(s->pwarn, -1, "SNDCTL_DSP_SETFMT failed");
	    goto fail;
	}
	warn(s->pwarn, -1, "playing 8bit sample");
	uss_8bit = 1;
    }
    /* no stereo */
    tmp = 0;
    st = ioctl(uss_fd, SNDCTL_DSP_STEREO, &tmp);
    if (st < 0 || tmp != 0)
    {
	warn(s->pwarn, -1, "SNDCTL_DSP_STEREO failed");
	goto fail;
    }
    /* speed */
    tmp = *speed;
    st = ioctl(uss_fd, SNDCTL_DSP_SPEED, &tmp);
    if (st < 0 || tmp <= 0)
    {
	warn(s->pwarn, -1, "SNDCTL_DSP_SPEED failed");
	goto fail;
    }
    *speed = tmp;
    /* fragments */
    for (tmp = 1; 1 << tmp < *fragsize; tmp++);
    orig = tmp = tmp + (*fragnr << 16) + !uss_8bit;
    st = ioctl(uss_fd, SNDCTL_DSP_SETFRAGMENT, &tmp);
    if (st < 0 || (tmp^orig)&0xffff)
    {
	warn(s->pwarn, -1, "SNDCTL_DSP_SETFRAGMENT failed");
	goto fail;
    }
    if (tmp != orig)
    {
	if (tmp >> 16 > *fragnr)
	{
	    warn(s->pwarn, -1, "SNDCTL_DSP_SETFRAGMENT: too many fragments");
	    goto fail;
	}
	*fragnr = tmp >> 16;
	if (*fragnr < 3)
	{
	    warn(s->pwarn, -1, "SNDCTL_DSP_SETFRAGMENT: too few fragments");
	    goto fail;
	}
    }
    uss_bufsize = (*fragsize)*(*fragnr);
    uss_fragsize = *fragsize;
    return 0;
fail:
    close(uss_fd);
    uss_fd = -1;
    uss_8bit = 0;
    uss_bufsize = 0;
    uss_fragsize = 0;
    return 1;
}

static int uss_write(sound_t *s, s16_t *pbuf, int nr)
{
    int			total, i, now;
    if (uss_8bit)
    {
	/* XXX: ugly to change contents of the buffer */
	for (i = 0; i < nr; i++)
	    ((char *)pbuf)[i] = pbuf[i]/256 + 128;
	total = nr;
    }
    else
	total = nr*sizeof(s16_t);
    for (i = 0; i < total; i += now)
    {
	now = write(uss_fd, (char *)pbuf + i, total - i);
	if (now <= 0)
	{
	    if (now < 0)
		perror("uss_write");
	    return 1;
	}
    }
    return 0;
}

static int uss_bufferstatus(sound_t *s, int first)
{
    audio_buf_info		info;
    int				st, ret;

    st = ioctl(uss_fd, SNDCTL_DSP_GETOSPACE, &info);
    if (st < 0)
    {
	warn(s->pwarn, -1, "SNDCTL_DSP_GETOSPACE failed");
	return -1;
    }
    ret = info.fragments*info.fragsize;
    if (ret != info.bytes)
    {
	warn(s->pwarn, 11, "GETOSPACE: ret(%d)!=bytes(%d)", ret, info.bytes);
	ret = info.bytes;
    }
    if (ret < 0)
    {
        warn(s->pwarn, 12, "GETOSPACE: bytes < 0");
	ret = 0;
    }
    if (!uss_8bit)
	ret /= sizeof(s16_t);
    if (ret > uss_bufsize)
    {
	warn(s->pwarn, 13, "GETOSPACE: bytes > bufsize");
	ret = uss_bufsize;
    }
#if defined(linux)
    /*
     * GETOSPACE before first write returns random value (or actually the
     * value on which the device was when it was closed last time). I hope
     * this has been fixed after 'Sound Driver:3.5-beta2-960210'
     */
    if (first && !ret)
    {
	ret = 1;
	warn(s->pwarn, -1, "SNDCTL_DSP_GETOSPACE not reliable after open()");
    }
#endif
    return ret;
}

static void uss_close()
{
    close(uss_fd);
    uss_fd = -1;
    uss_8bit = 0;
    uss_bufsize = 0;
    uss_fragsize = 0;
}

static int uss_suspend(sound_t *s)
{
    int			 st;
    st = ioctl(uss_fd, SNDCTL_DSP_POST, NULL);
    if (st < 0)
    {
	warn(s->pwarn, -1, "SNDCTL_DSP_POST failed");
	return 1;
    }
    return 0;
}

static sid_device_t uss_device =
{
    "uss",
    uss_init,
    uss_write,
    NULL,
    NULL,
    uss_bufferstatus,
    uss_close,
    uss_suspend,
    NULL
};

#else
static sid_device_t uss_device;
#endif


/*
 * sgi sound device
 */
#if defined(sgi) && defined(HAVE_DMEDIA_AUDIO_H)
#include <dmedia/audio.h>
#if defined(HAVE_BSTRING_H)
#include <bstring.h>
#endif

static ALconfig    sgi_audioconfig = NULL;
static ALport      sgi_audioport = NULL;

static void sgi_errorhandler(long err, const char *msg, ...)
{
    printf("sgierrorhandler: %d, %s\n", (int)err, msg);
}

static int sgi_init(sound_t *s, char *param, int *speed,
		    int *fragsize, int *fragnr, double bufsize)
{
    long	chpars[] = {AL_OUTPUT_RATE, 0};
    int		st;

    ALseterrorhandler(sgi_errorhandler);
    chpars[1] = *speed;
    st = ALsetparams(AL_DEFAULT_DEVICE, chpars, 2);
    if (st < 0)
	return 1;
    st = ALgetparams(AL_DEFAULT_DEVICE, chpars, 2);
    if (st < 0)
	return 1;
    *speed = chpars[1];

    sgi_audioconfig = ALnewconfig();
    if (!sgi_audioconfig)
	return 1;
    st = ALsetchannels(sgi_audioconfig, AL_MONO);
    if (st < 0)
	goto fail;
    st = ALsetwidth(sgi_audioconfig, AL_SAMPLE_16);
    if (st < 0)
	goto fail;
    st = ALsetqueuesize(sgi_audioconfig, *fragsize * *fragnr);
    if (st < 0)
        goto fail;
    sgi_audioport = ALopenport("outport", "w", sgi_audioconfig);
    if (!sgi_audioport)
	goto fail;
    return 0;
fail:
    ALfreeconfig(sgi_audioconfig);
    sgi_audioconfig = NULL;
    return 1;
}

static int sgi_write(sound_t *s, s16_t *pbuf, int nr)
{
    int				i;
    i = ALwritesamps(sgi_audioport, pbuf, nr);
    if (i < 0)
	return 1;
    return 0;
}

static int sgi_bufferstatus(sound_t *s, int first)
{
    int				i;
    i = ALgetfilled(sgi_audioport);
    return i;
}

static void sgi_close()
{
    /* XXX: code missing */
    ALfreeconfig(sgi_audioconfig);
    sgi_audioconfig = NULL;
}

static sid_device_t sgi_device =
{
    "sgi",
    sgi_init,
    sgi_write,
    NULL,
    NULL,
    sgi_bufferstatus,
    sgi_close,
    NULL,
    NULL
};

#else
static sid_device_t sgi_device;
#endif


/*
 * Solaris (untested and unfinished)
 */
#if defined(sun) && defined(HAVE_SYS_AUDIOIO_H)
#include <sys/audioio.h>

static int sun_bufferstatus(sound_t *s, int first);

static int sun_fd = -1;
static int sun_8bit = 0;
static int sun_bufsize = 0;
static int sun_written = 0;

static int toulaw8(s16_t data)
{
    int			v, s, a;

    a = data / 8;

    v = (a < 0 ? -a : a);
    s = (a < 0 ? 0 : 0x80);

    if (v >= 4080)
        a = 0;
    else if (v >= 2032)
        a = 0x0f - (v - 2032) / 128;
    else if (v >= 1008)
        a = 0x1f - (v - 1008) / 64;
    else if (v >= 496)
        a = 0x2f - (v - 496) / 32;
    else if (v >= 240)
        a = 0x3f - (v - 240) / 16;
    else if (v >= 112)
        a = 0x4f - (v - 112) / 8;
    else if (v >= 48)
        a = 0x5f - (v - 48) / 4;
    else if (v >= 16)
        a = 0x6f - (v - 16) / 2;
    else
        a = 0x7f - v;

    a |= s;

    return a;
}


static int sun_init(sound_t *s, char *param, int *speed,
		    int *fragsize, int *fragnr, double bufsize)
{
    int			st;
    struct audio_info	info;

    if (!param)
	param = "/dev/audio";
    sun_fd = open(param, O_WRONLY, 0777);
    if (sun_fd < 0)
	return 1;
    AUDIO_INITINFO(&info);
    info.play.sample_rate = *speed;
    info.play.channels = 1;
    info.play.precision = 16;
    info.play.encoding = AUDIO_ENCODING_LINEAR;
    st = ioctl(sun_fd, AUDIO_SETINFO, &info);
    if (st < 0)
    {
	AUDIO_INITINFO(&info);
	info.play.sample_rate = 8000;
	info.play.channels = 1;
	info.play.precision = 8;
	info.play.encoding = AUDIO_ENCODING_ULAW;
	st = ioctl(sun_fd, AUDIO_SETINFO, &info);
	if (st < 0)
	    goto fail;
	sun_8bit = 1;
	*speed = 8000;
	warn(s->pwarn, -1, "playing 8 bit ulaw at 8000Hz");
    }
    sun_bufsize = (*fragsize)*(*fragnr);
    sun_written = 0;
    return 0;
fail:
    close(sun_fd);
    sun_fd = -1;
    return 1;
}

static int sun_write(sound_t *s, s16_t *pbuf, int nr)
{
    int			total, i, now;
    if (sun_8bit)
    {
	/* XXX: ugly to change contents of the buffer */
	for (i = 0; i < nr; i++)
	    ((char *)pbuf)[i] = toulaw8(pbuf[i]);
	total = nr;
    }
    else
	total = nr*sizeof(s16_t);
    for (i = 0; i < total; i += now)
    {
	now = write(sun_fd, (char *)pbuf + i, total - i);
	if (now <= 0)
	    return 1;
    }
    sun_written += nr;
    /* XXX: correct? */
    while (sun_bufferstatus(s, 0) > sun_bufsize)
	usleep(1000000 / (4 * (int)RFSH_PER_SEC));
    return 0;
}

static int sun_bufferstatus(sound_t *s, int first)
{
    int			st;
    struct audio_info	info;
    st = ioctl(sun_fd, AUDIO_GETINFO, &info);
    if (st < 0)
	return -1;
    /* XXX: is samples reliable? eof? */
    return sun_written - info.play.samples;
}

static void sun_close()
{
    close(sun_fd);
    sun_fd = -1;
    sun_8bit = 0;
    sun_bufsize = 0;
    sun_written = 0;
}


static sid_device_t sun_device =
{
    "sun",
    sun_init,
    sun_write,
    NULL,
    NULL,
    sun_bufferstatus,
    sun_close,
    NULL,
    NULL
};

#else
static sid_device_t sun_device;
#endif


#if defined(HAVE_LIBUMSOBJ) && defined(HAVE_UMS_UMSAUDIODEVICE_H) && defined(HAVE_UMS_UMSBAUDDEVICE_H)

/* AIX -support by Chris Sharp (sharpc@hursley.ibm.com) */

#include <UMS/UMSAudioDevice.h>
#include <UMS/UMSBAUDDevice.h>

/* XXX: AIX: can these be made static and use aix_ -prefix on these? */
UMSAudioDeviceMClass audio_device_class;
UMSAudioDevice_ReturnCode rc;
UMSBAUDDevice audio_device;
Environment *ev;
UMSAudioTypes_Buffer buffer;
UMSAudioDeviceMClass_ErrorCode audio_device_class_error;
char* error_string;
char* audio_formats_alias;
char* audio_inputs_alias;
char* audio_outputs_alias;
char* obyte_order;
long out_rate;
long left_gain, right_gain;


static int aix_init(sound_t *s, char *param, int *speed,
		     int *fragsize, int *fragnr, double bufsize)
{
    int	st, tmp, i;
    /* open device */
    ev = somGetGlobalEnvironment();
    audio_device = UMSBAUDDeviceNew();
    rc = UMSAudioDevice_open(audio_device, ev, "/dev/paud0", "PLAY",
			     UMSAudioDevice_BlockingIO);
    if (audio_device == NULL)
    {
    	fprintf(stderr,"can't create audio device object\nError: %s\n",
		error_string);
	return 1;
    }

    rc = UMSAudioDevice_set_volume(audio_device, ev, 100);
    rc = UMSAudioDevice_set_balance(audio_device, ev, 0);

    rc = UMSAudioDevice_set_time_format(audio_device, ev, UMSAudioTypes_Msecs);

    if (obyte_order)
        free(obyte_order);
    rc = UMSAudioDevice_set_byte_order(audio_device, ev, "LSB");

    /* set 16bit */
    rc = UMSAudioDevice_set_bits_per_sample(audio_device, ev, 16);
    rc = UMSAudioDevice_set_audio_format_type(audio_device, ev, "PCM");
    rc = UMSAudioDevice_set_number_format(audio_device, ev, "TWOS_COMPLEMENT");

    /* set speed */
    rc = UMSAudioDevice_set_sample_rate(audio_device, ev, *speed, &out_rate);

    /* channels */
    rc = UMSAudioDevice_set_number_of_channels(audio_device, ev, 1);

    /* should we use the default? */
    left_gain = right_gain = 100;
    rc = UMSAudioDevice_enable_output(audio_device, ev, "LINE_OUT",
				      &left_gain, &right_gain);

    /* set buffer size */
    tmp = (*fragsize)*(*fragnr)*sizeof(s16_t);
    buffer._maximum = tmp;
    buffer._buffer  = (char *) xmalloc(tmp);
    buffer._length = 0;


    rc = UMSAudioDevice_initialize(audio_device, ev);
    rc = UMSAudioDevice_start(audio_device, ev);

    return 0;
#if 0
    /* XXX: AIX: everything should check rc, this isn't used now */
fail:
    UMSAudioDevice_stop(audio_device, ev);
    UMSAudioDevice_close(audio_device, ev);
    _somFree(audio_device);
    free(buffer._buffer);
    audio_device = NULL;

    return 1;
#endif
}

static int aix_write(sound_t *s, s16_t *pbuf, int nr)
{
    int	total, i, now;
    long samples_written;

    total = nr*sizeof(s16_t);
    buffer._length = total;
    memcpy(buffer._buffer,pbuf,total);
    rc = UMSAudioDevice_write(audio_device, ev, &buffer, total,
			      &samples_written);
    return 0;
}

static int aix_bufferstatus(sound_t *s, int first)
{
    int i = -1;
    rc = UMSAudioDevice_write_buff_remain(audio_device, ev, &i);
    if (i < 0)
      return -1;
    /* fprintf(stderr,"Audio Buffer remains: %d\n blocks",i); */
    return i/sizeof(s16_t);
}

static void aix_close()
{
    rc = UMSAudioDevice_play_remaining_data(audio_device, ev, TRUE);
    UMSAudioDevice_stop(audio_device, ev);
    UMSAudioDevice_close(audio_device, ev);
    _somFree(audio_device);
    free(buffer._buffer);
    audio_device = NULL;
}


static sid_device_t aix_device =
{
    "aix",
    aix_init,
    aix_write,
    NULL,
    NULL,
    aix_bufferstatus,
    aix_close,
    NULL,
    NULL
};

#else
static sid_device_t aix_device;
#endif

#if defined(__hpux) && defined(HAVE_SYS_AUDIO_H)
#include <sys/audio.h>

static int hpux_fd = -1;

static int hpux_init(sound_t *s, char *param, int *speed,
		     int *fragsize, int *fragnr, double bufsize)
{
    int				st, tmp, i;
    if (!param)
	param = "/dev/audio";
    /* open device */
    hpux_fd = open(param, O_WRONLY, 0777);
    if (hpux_fd < 0)
	return 1;
    /* set 16bit */
    st = ioctl(hpux_fd, AUDIO_SET_DATA_FORMAT, AUDIO_FORMAT_LINEAR16BIT);
    if (st < 0)
	goto fail;
    /* set speed */
    st = ioctl(hpux_fd, AUDIO_SET_SAMPLE_RATE, *speed);
    if (st < 0)
	goto fail;
    /* channels */
    st = ioctl(hpux_fd, AUDIO_SET_CHANNELS, 1);
    if (st < 0)
	goto fail;
    /* should we use the default? */
    st = ioctl(hpux_fd, AUDIO_SET_OUTPUT, AUDIO_OUT_SPEAKER);
    if (st < 0)
	goto fail;
    /* set buffer size */
    tmp = (*fragsize)*(*fragnr)*sizeof(s16_t);
    st = ioctl(hpux_fd, AUDIO_SET_TXBUFSIZE, tmp);
    if (st < 0)
    {
	/* XXX: what are valid buffersizes? */
	for (i = 1; i < tmp; i *= 2);
	tmp = i;
	st = ioctl(hpux_fd, AUDIO_SET_TXBUFSIZE, tmp);
	if (st < 0)
	    goto fail;
	*fragnr = tmp / ((*fragsize)*sizeof(s16_t));
    }
    return 0;
fail:
    close(hpux_fd);
    hpux_fd = -1;
    return 1;
}

static int hpux_write(sound_t *s, s16_t *pbuf, int nr)
{
    int			total, i, now;
    total = nr*sizeof(s16_t);
    for (i = 0; i < total; i += now)
    {
	now = write(hpux_fd, (char *)pbuf + i, total - i);
	if (now <= 0)
	    return 1;
    }
    return 0;
}

static int hpux_bufferstatus(sound_t *s, int first)
{
    int				st;
    struct audio_status		ast;
    st = ioctl(hpux_fd, AUDIO_GET_STATUS, &ast);
    if (st < 0)
	return -1;
    return ast.transmit_buffer_count / sizeof(s16_t);
}

static void hpux_close()
{
    close(hpux_fd);
    hpux_fd = -1;
}


static sid_device_t hpux_device =
{
    "hpux",
    hpux_init,
    hpux_write,
    NULL,
    NULL,
    hpux_bufferstatus,
    hpux_close,
    NULL,
    NULL
};

#else
static sid_device_t hpux_device;
#endif

#ifdef __MSDOS__

/*
 * MIDAS
 */

#include "vmidas.h"

static int midas_bufferstatus(sound_t *s, int first);

static MIDASstreamHandle midas_stream = NULL;
static int midas_bufsize = -1;
static int midas_maxsize = -1;

static int midas_init(sound_t *s, char *param, int *speed,
		      int *fragsize, int *fragnr, double bufsize)
{
    BOOL		st;

    st = vmidas_startup();
    if (st != TRUE)
	return 1;
    st = MIDASsetOption(MIDAS_OPTION_MIXRATE, *speed);
    if (st != TRUE)
	return 1;
    st = MIDASsetOption(MIDAS_OPTION_MIXING_MODE, MIDAS_MIX_NORMAL_QUALITY);
    if (st != TRUE)
	return 1;
    st = MIDASsetOption(MIDAS_OPTION_OUTPUTMODE, MIDAS_MODE_16BIT_MONO);
    if (st != TRUE)
	return 1;
    st = MIDASsetOption(MIDAS_OPTION_MIXBUFLEN,
			(*fragsize)*(*fragnr)*sizeof(s16_t));
    if (st != TRUE)
	return 1;
    st = MIDASsetOption(MIDAS_OPTION_MIXBUFBLOCKS, *fragnr);
    if (st != TRUE)
	return 1;
#ifdef __MSDOS__
#if 0
    st = MIDASconfig();
    if (st != TRUE)
	return 1;
#endif
#endif
    st = vmidas_init();
    if (st != TRUE)
	return 1;
    st = MIDASopenChannels(1);
    if (st != TRUE)
    {
	/* st = MIDASclose(); */
	return 1;
    }
    midas_stream = MIDASplayStreamPolling(MIDAS_SAMPLE_16BIT_MONO, *speed,
					  (int)(bufsize*1000));
    if (!midas_stream)
    {
	st = MIDAScloseChannels();
	/* st = MIDASclose(); */
	return 1;
    }
    midas_bufsize = (*fragsize)*(*fragnr);
    midas_maxsize = midas_bufsize / 2;
    return 0;
}

static int midas_write(sound_t *s, s16_t *pbuf, int nr)
{
    BOOL		st = 1;
    unsigned int	ist;

    ist = MIDASfeedStreamData(midas_stream, (unsigned char *)pbuf,
			      nr*sizeof(s16_t), TRUE);
    if (ist != nr*sizeof(s16_t))
	return 1;
#ifndef __MSDOS__
    st = MIDASpoll();
#endif
    return !st;
}

static int midas_bufferstatus(sound_t *s, int first)
{
    int			nr;
    if (first)
	return 0;
    nr = MIDASgetStreamBytesBuffered(midas_stream);
    if (nr < 0)
	nr = 0;
    nr /= sizeof(s16_t);
    if (nr > midas_maxsize)
	midas_maxsize = nr;
    return (int)((double)nr/midas_maxsize*midas_bufsize);
}

static void midas_close()
{
    BOOL		st;

    /* XXX: we might come here from `atexit', so MIDAS might have been shut
       down already.  This is a dirty kludge, we should find a cleaner way to
       do it. */
    if (vmidas_available())
    {
	st = MIDASstopStream(midas_stream);
	st = MIDAScloseChannels();
	/* st = MIDASclose(); */
    }
    midas_stream = NULL;
    midas_bufsize = -1;
    midas_maxsize = -1;
}

static sid_device_t midas_device =
{
    "midas",
    midas_init,
    midas_write,
    NULL,
    NULL,
    midas_bufferstatus,
    midas_close,
    NULL,
    NULL
};
#else
static sid_device_t midas_device;
#endif


#if defined(HAVE_SDL_AUDIO_H) && defined(HAVE_SDL_SLEEP_H)
#include "SDL_audio.h"
#include "SDL_sleep.h"

static s16_t *sdl_buf = NULL;
static const SDL_AudioSpec *sdl_spec = NULL;
static volatile int sdl_inptr = 0;
static volatile int sdl_outptr = 0;
static volatile int sdl_len = 0;

static void sdl_callback(void *userdata, Uint8 *stream, Uint16 len,
			 Uint8 *lookahead)
{
    int			amount, total;
    total = 0;
    while (total < len/sizeof(s16_t))
    {
	amount = sdl_inptr - sdl_outptr;
	if (amount < 0)
	    amount = sdl_len - sdl_outptr;
	if (amount + total > len/sizeof(s16_t))
	    amount = len/sizeof(s16_t) - total;
	if (!amount)
	{
	    if (!sdl_spec)
	    {
		memset(stream + total*sizeof(s16_t), 0,
		       len - total*sizeof(s16_t));
		return;
	    }
	    Sleep(5);
	    continue;
	}
	memcpy(stream + total*sizeof(s16_t), sdl_buf + sdl_outptr,
	       amount*sizeof(s16_t));
	total += amount;
	sdl_outptr += amount;
	if (sdl_outptr == sdl_len)
	    sdl_outptr = 0;
    }
}

static int sdl_init(sound_t *s, char *param, int *speed,
		    int *fragsize, int *fragnr, double bufsize)
{
    SDL_AudioSpec		spec;
    memset(&spec, 0, sizeof(spec));
    spec.freq = *speed;
    spec.format = AUDIO_S16 | AUDIO_MONO;
    spec.samples = *fragsize;
    spec.callback = sdl_callback;
    sdl_spec = SDL_OpenAudio(&spec);
    if (!sdl_spec)
	return 1;
    if (sdl_spec->format != (AUDIO_S16 | AUDIO_MONO))
    {
	sdl_spec = NULL;
	SDL_CloseAudio();
	return 1;
    }
    sdl_len = (*fragsize)*(*fragnr) + 1;
    sdl_inptr = sdl_outptr = 0;
    sdl_buf = xmalloc(sizeof(s16_t)*sdl_len);
    if (!sdl_buf)
    {
	SDL_CloseAudio();
	return 1;
    }
    *speed = sdl_spec->freq;
    SDL_PauseAudio(0);
    return 0;
}

static int sdl_write(sound_t *s, s16_t *pbuf, int nr)
{
    int			total, amount;
    total = 0;
    while (total < nr)
    {
	amount = sdl_outptr - sdl_inptr;
	if (amount <= 0)
	    amount = sdl_len - sdl_inptr;
	if ((sdl_inptr + amount)%sdl_len == sdl_outptr)
	    amount--;
	if (amount <= 0)
	{
	    Sleep(5);
	    continue;
	}
	if (total + amount > nr)
	    amount = nr - total;
	memcpy(sdl_buf + sdl_inptr, pbuf + total, amount*sizeof(s16_t));
	sdl_inptr += amount;
	total += amount;
	if (sdl_inptr == sdl_len)
	    sdl_inptr = 0;
    }
    return 0;
}

static int sdl_bufferstatus(sound_t *s, int first)
{
    int		amount;
    amount = sdl_inptr - sdl_outptr;
    if (amount < 0)
	amount += sdl_len;
    return amount;
}

static void sdl_close()
{
    sdl_spec = NULL;
    SDL_CloseAudio();
    free(sdl_buf);
    sdl_buf = NULL;
    sdl_inptr = sdl_outptr = sdl_len = 0;
}


static sid_device_t sdl_device =
{
    "sdl",
    sdl_init,
    sdl_write,
    NULL,
    NULL,
    sdl_bufferstatus,
    sdl_close,
    NULL,
    NULL
};

#else
static sid_device_t sdl_device;
#endif


static sid_device_t *sid_devices[13] =
{
    &uss_device,
    &sgi_device,
    &sun_device,
    &hpux_device,
    &aix_device,
    &midas_device,
    &sdl_device,
    &dummy_device,
    &fs_device,
    &speed_device,
    &dump_device,
    &test_device,
    NULL
};

/*
 * and the code itself
 */

#define BUFSIZE 32768
typedef struct
{
    /* sid itself */
    sound_t		 sid;
    /* number of clocks between each sample. used value */
    double		 clkstep;
    /* number of clocks between each sample. original value */
    double		 origclkstep;
    /* factor between those two clksteps */
    double		 clkfactor;
    /* time of last sample generated */
    double		 fclk;
    /* time of last sid.clock() */
    CLOCK		 sidclk;
    /* time of last write to sid. used for pdev->dump() */
    CLOCK		 wclk;
    /* sample buffer */
    s16_t		 buffer[BUFSIZE];
    /* pointer to device structure in use */
    sid_device_t	*pdev;
    /* number of samples in a fragment */
    int			 fragsize;
    /* number of fragments in kernel buffer */
    int			 fragnr;
    /* number of samples in kernel buffer */
    int			 bufsize;
    /* return value of first pdev->bufferstatus() call to device */
    int			 firststatus;
    /* constants related to adjusting sound */
    int			 prevused;
    int			 prevfill;
    /* is the device suspended? */
    int			 issuspended;
    s16_t		 lastsample;
} siddata_t;

static siddata_t siddata;

/* close sid device and show error dialog if needed */
static int closesid(char *msg)
{
    if (siddata.pdev)
    {
	warn(siddata.sid.pwarn, -1, "closing device '%s'", siddata.pdev->name);
	if (siddata.pdev->close)
	    siddata.pdev->close();
	siddata.pdev = NULL;
    }
    if (msg)
    {
        suspend_speed_eval();
	if (strcmp(msg, ""))
	{
	    UiError(msg);
	    app_resources.sound = 0;
	    UiUpdateMenus();
	}
    }
    siddata.prevused = siddata.prevfill = 0;
    return 1;
}

/* code to disable sid for a given number of seconds if needed */
static int disabletime;

static void suspendsid(char *reason)
{
    disabletime = time(0);
    warn(siddata.sid.pwarn, -1, "SUSPEND: disabling sid for %d secs (%s)",
	 app_resources.soundSuspendTime, reason);
    closesid("");
}

static void enablesid()
{
    int		now, diff;
    if (!disabletime)
        return;
    now = time(0);
    diff = now - disabletime;
    if (diff < 0 || diff >= app_resources.soundSuspendTime)
    {
        warn(siddata.sid.pwarn, -1, "ENABLE");
        disabletime = 0;
    }
}

/* open sound device */
static int initsid()
{
    int					 i, tmp;
    sid_device_t			*pdev;
    char				*name;
    char				*param;
    int					 speed;
    int					 fragsize;
    int					 fragnr;
    double				 bufsize;
    char				 err[1024];

    if (app_resources.soundSuspendTime > 0 && disabletime)
        return 1;

    name = app_resources.soundDeviceName;
    param = app_resources.soundDeviceArg;
    tmp = app_resources.soundBufferSize;
    if (tmp < 100 || tmp > 1000)
	tmp = SOUND_SAMPLE_BUFFER_SIZE;
    bufsize = tmp / 1000.0;

    speed = app_resources.soundSampleRate;
    if (speed < 8000 || speed > 50000)
	speed = SOUND_SAMPLE_RATE;
    /* calculate optimal fragments */
    fragsize = speed / FRAGS_PER_SECOND;
    for (i = 1; 1 << i < fragsize; i++);
    fragsize = 1 << i;
    fragnr = (int)((speed*bufsize + fragsize - 1) / fragsize);
    if (fragnr < 3)
        fragnr = 3;

    for (i = 0; (pdev = sid_devices[i]); i++)
    {
	if ((name && pdev->name && !strcmp(pdev->name, name)) ||
	    (!name && pdev->name))
	{
	    if (pdev->init)
	    {
		tmp = pdev->init(&siddata.sid, param, &speed,
				 &fragsize, &fragnr, bufsize);
		if (tmp)
		{
		    sprintf(err, "Audio: initialization failed for device `%s'.",
			    pdev->name);
		    return closesid(err);
		}
	    }
	    siddata.issuspended = -1;
	    siddata.lastsample = 0;
	    siddata.pdev = pdev;
	    siddata.fragsize = fragsize;
	    siddata.fragnr = fragnr;
	    siddata.bufsize = fragsize*fragnr;
	    warn(siddata.sid.pwarn, -1,
		 "opened device '%s' speed %dHz fragsize %.3fs bufsize %.3fs",
		 pdev->name, speed, (double)fragsize / speed,
		 (double)siddata.bufsize / speed);
	    app_resources.soundSampleRate = speed;
	    if (pdev->write)
		init_sid(&siddata.sid, siddata.buffer, speed);
	    else
		init_sid(&siddata.sid, NULL, speed);
	    if (pdev->bufferstatus)
		siddata.firststatus = pdev->bufferstatus(&siddata.sid, 1);
	    siddata.clkstep = (double)CYCLES_PER_SEC / speed;
	    siddata.origclkstep = siddata.clkstep;
	    siddata.clkfactor = 1.0;
	    siddata.fclk = clk;
	    siddata.sidclk = clk;
	    siddata.wclk = clk;
	    return 0;
	}
    }
    sprintf(err, "Audio: device `%s' not found or not supported.", name);
    return closesid(err);
}

/* run sid */
static int run_sid()
{
    int				i;
    if (!app_resources.sound)
	return 1;
    if (app_resources.soundSuspendTime > 0 && disabletime)
        return 1;
    if (!siddata.pdev)
    {
	i = initsid();
	if (i)
	    return i;
    }

    int sample_count = int((clk - siddata.fclk)/siddata.clkstep);

    if (siddata.sid.bufptr + sample_count > BUFSIZE)
	return closesid("Audio: sound buffer overflow.");

    while (siddata.fclk + siddata.clkstep <= clk) {
      siddata.fclk += siddata.clkstep;
      cycle_count delta_t = cycle_count(siddata.fclk - siddata.sidclk);
      if (delta_t > 0) {
	siddata.sidclk += delta_t;
	sid.clock(delta_t);
      }

      if (siddata.sid.pbuf) {
	siddata.sid.pbuf[siddata.sid.bufptr++] = sid.output();
      }
    }

    sid.clock(clk - siddata.sidclk);
    siddata.sidclk = clk;

    return 0;
}

/* flush all generated samples from buffer to sounddevice. adjust sid runspeed
   to match real running speed of program */
int flush_sound()
{
    int			i, nr, space, used, fill = 0;

    if (app_resources.soundSuspendTime > 0)
        enablesid();
    i = run_sid();
    if (i)
	return 0;
    resume_sound();
    if (siddata.pdev->flush)
    {
	i = siddata.pdev->flush(&siddata.sid);
	if (i)
	{
	    closesid("Audio: cannot flush.");
	    return 0;
	}
    }
    if (siddata.sid.bufptr < siddata.fragsize)
	return 0;
    nr = siddata.sid.bufptr - siddata.sid.bufptr % siddata.fragsize;
    /* adjust speed */
    if (siddata.pdev->bufferstatus)
    {
	space = siddata.pdev->bufferstatus(&siddata.sid, 0);
	if (!siddata.firststatus)
	    space = siddata.bufsize - space;
	used = siddata.bufsize - space;
	if (space < 0 || used < 0)
	{
	    warn(siddata.sid.pwarn, -1, "fragment problems %d %d %d",
		 space, used, siddata.firststatus);
	    closesid("Audio: fragment problems.");
	    return 0;
	}
	/* buffer empty */
	if (used <= siddata.fragsize)
	{
	    s16_t		*p, v;
	    int			 j;
	    static int		 prev;
	    int			 now;
	    if (app_resources.soundSuspendTime > 0)
	    {
	        now = time(0);
		if (now == prev)
		{
		    suspendsid("buffer overruns");
		    return 0;
		}
		prev = now;
	    }
	    j = siddata.fragsize*siddata.fragnr - nr;
	    if (j > siddata.bufsize / 2 &&
		!app_resources.soundSpeedAdjustment &&
		app_resources.speed)
	    {
		j = siddata.fragsize*(siddata.fragnr/2);
	    }
	    j *= sizeof(*p);
	    if (j > 0)
	    {
	        p = (s16_t*)xmalloc(j);
		v = siddata.sid.bufptr > 0 ? siddata.buffer[0] : 0;
		for (i = 0; (size_t)i < j / sizeof(*p); i++)
		    p[i] = (short)((float)v*i/(j / sizeof(*p)));
		i = siddata.pdev->write(&siddata.sid, p,
					j / sizeof(*p));
		free(p);
		if (i)
		{
		    closesid("Audio: write to sound device failed.");
		    return 0;
		}
		siddata.lastsample = v;
	    }
	    fill = j;
	}
	if (!app_resources.soundSpeedAdjustment &&
	    app_resources.speed > 0)
	    siddata.clkfactor = app_resources.speed / 100.0;
	else
	{
	    if (siddata.prevfill)
		siddata.prevused = used;
	    siddata.clkfactor *= 1.0 + 0.9*(used - siddata.prevused)/
		siddata.bufsize;
	}
	siddata.prevused = used;
	siddata.prevfill = fill;
	siddata.clkfactor *= 0.9 + (used+nr)*0.12/siddata.bufsize;
	siddata.clkstep = siddata.origclkstep * siddata.clkfactor;
	if (CYCLES_PER_RFSH / siddata.clkstep >= siddata.bufsize)
	{
	    if (app_resources.soundSuspendTime > 0)
	        suspendsid("running too slow");
	    else
	        closesid("Audio: running too slow.");
	    return 0;
	}
	if (nr > space && nr < used)
	    nr = space;
    }
    i = siddata.pdev->write(&siddata.sid, siddata.buffer, nr);
    if (i)
    {
	closesid("Audio: write to sounddevice failed.");
	return 0;
    }
    siddata.lastsample = siddata.buffer[nr-1];
    siddata.sid.bufptr -= nr;
    if (siddata.sid.bufptr > 0)
    {
	for (i = 0; i < siddata.sid.bufptr; i++)
	    siddata.buffer[i] = siddata.buffer[i + nr];
    }
    return 0;
}

/* close sid */
void close_sound()
{
    closesid(NULL);
}

/* suspend sid (eg. before pause) */
void suspend_sound(void)
{
    int				 i;
    s16_t			*p, v;
    if (siddata.pdev)
    {
	if (siddata.pdev->write && siddata.issuspended == 0)
	{
	    p = (s16_t*)xmalloc(siddata.fragsize*sizeof(s16_t));
	    if (!p)
		return;
	    v = siddata.lastsample;
	    for (i = 0; i < siddata.fragsize; i++)
		p[i] = (short)(v - (float)v*i/siddata.fragsize);
	    free(p);
	    i = siddata.pdev->write(&siddata.sid, p, siddata.fragsize);
	    if (i)
		return;
	}
	if (siddata.pdev->suspend && siddata.issuspended == 0)
	{
	    i = siddata.pdev->suspend(&siddata.sid);
	    if (i)
		return;
	}
	siddata.issuspended = 1;
    }
}

/* resume sid */
void resume_sound(void)
{
    int				i;
    if (siddata.pdev)
    {
	if (siddata.pdev->resume && siddata.issuspended == 1)
	{
	    i = siddata.pdev->resume(&siddata.sid);
	    siddata.issuspended = i ? 1 : 0;
	}
	else
	    siddata.issuspended = 0;
    }
}

/* initialize sid at program start -time */
void initialize_sound()
{
    /* dummy init to get pwarn */
    init_sid(&siddata.sid, NULL, SOUND_SAMPLE_RATE);
}

/* adjust clk before overflow */
void sid_prevent_clk_overflow()
{
    if (!siddata.pdev)
	return;
    siddata.wclk -= PREVENT_CLK_OVERFLOW_SUB;
    siddata.sidclk -= PREVENT_CLK_OVERFLOW_SUB;
    siddata.fclk -= PREVENT_CLK_OVERFLOW_SUB;
}


BYTE REGPARM1 read_sid(ADDRESS addr)
{
  run_sid();

  addr &= 0x1f;
  return sid.read(addr);
}


void REGPARM2 store_sid(ADDRESS addr, BYTE byte)
{
  addr &= 0x1f;

  int i = run_sid();
  if (!i && siddata.pdev->dump) {
    i = siddata.pdev->dump(addr, byte, clk - siddata.wclk);
    siddata.wclk = clk;
    if (i)
      closesid("Audio: store to sounddevice failed.");
  }

  sid.write(addr, byte);
}


void reset_sid()
{
  sid.reset();

  // maincpu.c::reset() first calls this function,
  // then sets clk = 6 without resetting the clock variables below.
  siddata.fclk = 0;
  siddata.sidclk = 0;
  siddata.wclk = 0;
}


} // extern "C"
