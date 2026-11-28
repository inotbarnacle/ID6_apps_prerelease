#define _POSIX_C_SOURCE 200809L
/* Short, low-amplitude alert played by a separate QNX process. */
#include <dlfcn.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/asoundlib.h>
#include <unistd.h>

int boost_alert_tone(void) {
    enum { rate=48000, frames=1024, fragments=9, amplitude=1000 };
    alarm(3);
    void *library=dlopen("/opt/mm/lib/libasound/libasound.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) library=dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!library) return 1;

    int (*open_pcm)(snd_pcm_t**, int*, int*, int)=dlsym(library,"snd_pcm_open_preferred");
    int (*close_pcm)(snd_pcm_t*)=dlsym(library,"snd_pcm_close");
    int (*configure)(snd_pcm_t*, snd_pcm_channel_params_t*)=dlsym(library,"snd_pcm_plugin_params");
    int (*prepare)(snd_pcm_t*, int)=dlsym(library,"snd_pcm_plugin_prepare");
    ssize_t (*write_pcm)(snd_pcm_t*, const void*, size_t)=dlsym(library,"snd_pcm_plugin_write");
    int (*play_out)(snd_pcm_t*, int)=dlsym(library,"snd_pcm_plugin_flush");
    snd_pcm_t *pcm=NULL;
    int result=1;
    if (open_pcm && close_pcm && configure && prepare && write_pcm && play_out &&
        open_pcm(&pcm,NULL,NULL,SND_PCM_OPEN_PLAYBACK)==0) {
        snd_pcm_channel_params_t params;
        int16_t samples[frames*2];
        memset(&params,0,sizeof params);
        params.channel=SND_PCM_CHANNEL_PLAYBACK;
        params.mode=SND_PCM_MODE_BLOCK;
        params.start_mode=SND_PCM_START_FULL;
        params.stop_mode=SND_PCM_STOP_STOP;
        params.buf.block.frag_size=sizeof samples;
        params.buf.block.frags_min=2;
        params.buf.block.frags_max=4;
        params.format.interleave=1;
        params.format.format=SND_PCM_SFMT_S16_LE;
        params.format.rate=rate;
        params.format.voices=2;
        if (configure(pcm,&params)==0 && prepare(pcm,SND_PCM_CHANNEL_PLAYBACK)==0) {
            unsigned part;
            for (part=0;part<fragments;++part) {
                for (unsigned i=0;i<frames;++i) {
                    const unsigned n=part*frames+i;
                    const double fade=n<960 ? n/960.0 :
                        n>frames*fragments-1440 ? (frames*fragments-n)/1440.0 : 1.0;
                    samples[i*2]=samples[i*2+1]=(int16_t)(amplitude*fade*
                        sin(n * (6.283185307179586 * 720.0 / rate)));
                }
                if (write_pcm(pcm,samples,sizeof samples)!=(ssize_t)sizeof samples) break;
            }
            if (part==fragments && play_out(pcm,SND_PCM_CHANNEL_PLAYBACK)>=0) result=0;
        }
        close_pcm(pcm);
    }
    dlclose(library);
    return result;
}
