#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "sbc/sbc.h"
//#include "a2dp-codecs.h"
#include "rtp.h"

sbc_t sbct;

// 1024/44100 in nanosecs
#define PERIOD 23219955

int transportsock;
uint16_t seqnum = 0;
uint32_t timestamp = 0;
uint8_t inbuf[4096];
uint8_t outbuf[1024];
struct rtp_header* rtph = outbuf;
struct rtp_payload* rtpp = outbuf + sizeof(struct rtp_header);
int rtpsize = sizeof(struct rtp_header) + sizeof(struct rtp_payload);
FILE* wavfile;
int32_t nsamples;
volatile int ticks = 0;
volatile bool done = false;

// these routines assume write-mtu == 1000
// if packet size is less then fewer frames can be sent at a time

bool chunk_to_a2dp(void)
{
    unsigned int written;
    int n, k;
    uint8_t *d, *p, *end;
    uint8_t frame, nf;

    rtph->sequence_number = htons(seqnum++);
    rtph->timestamp = htonl(timestamp);
    k = fread(inbuf, 2, 1024, wavfile);
    if (k==0)  return true;
    p = inbuf;
    d = outbuf + rtpsize;
    end = outbuf+1000;
    rtpp->frame_count = nf = k/128;

    for (frame=0; frame<nf; frame++) {
    	sbc_encode(&sbct, p, 256, d, end-d, &written);
        p+=256;  d+=written;
    }

    n = d - outbuf;
    send(transportsock, outbuf, n, 0);
    timestamp += 1024;
    nsamples -= k;
    return (nsamples<=0);
}

bool stereo_chunk_to_a2dp(void)
{
    unsigned int written;
    int n, k;
    uint8_t *d, *p, *end;
    uint8_t frame, nf;

    rtph->sequence_number = htons(seqnum++);
    rtph->timestamp = htonl(timestamp);

    k = fread(inbuf, 4, 1024, wavfile);
    if (k==0)  return true;
    p = inbuf;
    d = outbuf + rtpsize;
    end = outbuf+1000;
    rtpp->frame_count = nf = k/128;

    for (frame=0; frame<nf; frame++) {
        sbc_encode(&sbct, p, 512, d, end-d, &written);
        p+=512;  d+=written;
    }

    n = d - outbuf;
    send(transportsock, outbuf, n, 0);
    timestamp += 1024;
    nsamples -= k;
    return (nsamples<=0);
}

static void sighandler(int sig, siginfo_t *si, void *uc)
{
    done = chunk_to_a2dp();
    ticks++;
}

static void stereosighandler(int sig, siginfo_t *si, void *uc)
{
    done = stereo_chunk_to_a2dp();
    ticks++;
}

void init_sbc(bool mono)
{
// this should be done in accordance with the config of the transport
// 2nd byte of config ==21 -> 8 subbands 16 blocks loundness-alloc
    sbc_init(&sbct, 0);
    if (mono)  sbct.mode = SBC_MODE_MONO;
    else  sbct.mode = SBC_MODE_STEREO;
    sbct.frequency = SBC_FREQ_44100;
    sbct.allocation = SBC_AM_LOUDNESS;
    sbct.subbands = SBC_SB_8;
    sbct.blocks = SBC_BLK_16;
    sbct.bitpool = 53;
    sbct.endian = SBC_LE;
}

static char pump_doc[] = "encode and stream file to a2dp sink";

static PyObject* _datapump(PyObject *self, PyObject *args)
{
    char *filename;
    timer_t timerid;
    struct sigevent sev;
    struct itimerspec its;
    sigset_t mask;
    struct sigaction sa;
    int t;
    uint32_t datasize;
    uint16_t numchans;
    bool mono;

    if (!PyArg_ParseTuple(args, "isH", &transportsock, &filename, &numchans))  return NULL;
    mono = (numchans<2);
    printf("transportsock=%d. file=%s   pumping...\n", transportsock, filename);

    sa.sa_flags = SA_SIGINFO;
    if (mono)  sa.sa_sigaction = sighandler;
    else  sa.sa_sigaction = stereosighandler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, NULL) == -1) { printf("sigaction");  return NULL; }

    sigemptyset(&mask);
    sigaddset(&mask, SIGRTMIN);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) { printf("sigprocmask");  return NULL; }

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) { printf("timer_create");  return NULL; }

    // first callback in 11.61ms  then every 23.22ms
    // so remote end never runs out of data
    its.it_value.tv_sec = 0;  its.it_value.tv_nsec = PERIOD>>1;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = PERIOD;
    if (timer_settime(timerid, 0, &its, NULL) == -1)  { printf("timer_settime");  return NULL; }

    init_sbc(mono);
    memset(rtph, 0, rtpsize);
    rtph->v=2; rtph->pt=1;  rtph->ssrc=htonl(1);
 //   unsigned int framelen, codesize, ms;
 //   framelen = sbc_get_frame_length(&sbct);
 //   codesize = sbc_get_codesize(&sbct);
 //   ms = sbc_get_frame_duration(&sbct);
 //   printf("framlen=%u  codesize=%u  duration=%u\n", framelen, codesize, ms);

    wavfile = fopen(filename, "r");  //TODO check wavfile open
    fseek(wavfile, 40, SEEK_SET);
    fread(&datasize, 4, 1, wavfile);
    if (mono) {
        nsamples = datasize >> 1;
        printf("nsamples=%u\n", nsamples);
        chunk_to_a2dp();
    }
    else {
       nsamples = datasize >> 2;
        printf("nsamples=%u\n", nsamples);
        stereo_chunk_to_a2dp();
    }
    printf("unbloking\n");
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)  { printf("sigprocmask unblock");  return NULL; }

    t = ticks;
    while (!done) {
        if (t != ticks) {
            if ((ticks & 0xf) == 0)  printf("%d\n", ticks);
            t=ticks;
        }
    }
   timer_delete(timerid);
   fclose(wavfile);
   sbc_finish(&sbct);
   Py_INCREF(Py_None);
   return Py_None;
}

static PyMethodDef Pump_methods[] = {
    {"datapump", _datapump, METH_VARARGS, pump_doc },
    { NULL, NULL, 0, NULL } };

static struct PyModuleDef _pumpmodule = {
    PyModuleDef_HEAD_INIT,
    "_Pump",
    "NULL",
    -1,
     Pump_methods };

PyMODINIT_FUNC PyInit__Pump(void)
{
    PyObject *mod = PyModule_Create(&_pumpmodule);
    return mod;
}
