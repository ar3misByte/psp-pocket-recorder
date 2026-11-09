#include "pspkernel.h"
#include "pspaudio.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
static sem_t sems[16]; static int nsem=0;
int sceKernelCreateSema(const char*n,int a,int init,int max,void*o){sem_init(&sems[nsem],0,init);return nsem++;}
int sceKernelWaitSema(SceUID s,int n,SceUInt*to){ if(!to){sem_wait(&sems[s]);return 0;} struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts); ts.tv_nsec+= (long)*to*1000; ts.tv_sec+=ts.tv_nsec/1000000000; ts.tv_nsec%=1000000000; return sem_timedwait(&sems[s],&ts)==0?0:-1;}
int sceKernelSignalSema(SceUID s,int n){sem_post(&sems[s]);return 0;}
int sceKernelPollSema(SceUID s,int n){return sem_trywait(&sems[s])==0?0:-1;}
typedef struct{int(*f)(SceSize,void*);pthread_t t;}Th; static Th ths[8]; static int nth=0;
static void* tramp(void*a){Th*t=a;t->f(0,0);return 0;}
SceUID sceKernelCreateThread(const char*n,int(*f)(SceSize,void*),int p,int s,int a,void*o){ths[nth].f=f;return nth++;}
int sceKernelStartThread(SceUID t,SceSize a,void*p){pthread_create(&ths[t].t,0,tramp,&ths[t]);return 0;}
int sceKernelDelayThread(SceUInt us){usleep(us/SPEED);return 0;}
int sceKernelWaitThreadEnd(SceUID t,SceUInt*to){pthread_join(ths[t].t,0);return 0;}
int sceKernelDeleteThread(SceUID t){return 0;}
void sceKernelDcacheWritebackRange(const void*p,unsigned int n){}
int sceAudioChReserve(int c,int n,int f){return 0;} int sceAudioChRelease(int c){return 0;}
int sceAudioOutputBlocking(int c,int v,void*b){usleep(23000/SPEED);return 0;}
/* SRC: models hardware that keeps reading the previous buffer until this call returns */
FILE *mock_out; static short *q[2]={0,0}; static int srcn=0, srcrate=0;
int sceAudioSRCChReserve(int n,int r,int c){srcn=n;srcrate=r;return 0;}
int sceAudioSRCChRelease(void){return 0;}
int sceAudioSRCOutputBlocking(int v,void*b){
  usleep((useconds_t)(1e6*srcn/srcrate/SPEED));
  if(q[0]) fwrite(q[0],4,srcn,mock_out);   /* hardware reads a buffer two calls late */
  q[0]=q[1]; q[1]=b; return 0;}
void mock_flush(void){ for(int i=0;i<2;i++) if(q[i]) fwrite(q[i],4,srcn,mock_out); }
