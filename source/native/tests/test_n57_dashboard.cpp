#include "n57_dashboard.h"
#include <assert.h>
#include <fstream>
#include <string>
#include <time.h>

static int pin=1;
extern "C" int n57_settings(unsigned* value){*value=0x010203;return pin;}

class Model : public N57Dashboard {
public:
    unsigned packets,live;
    Model(bmw::IDispatcher& d,const bmw::Bundle& b,const char* worker,unsigned port)
        :N57Dashboard(d,bmw::BasicString<char>("Test"),1,0,bmw::RHMIInterfaceVersion(),b,b,
                      worker,"/missing-test-gateway",port,BD_BIT(BD_RPM)),packets(0),live(0){}
    virtual void render(uint64_t now){double value=0;live=available(BD_RPM,now,value) && value==800;}
    virtual void packetReceived(const bd_frame&){packets++;}
    void graph(bool show){setViewVisible(2u,show);}
};

static void pause_ms(unsigned ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000};nanosleep(&t,0);}
static std::string log_text(const char* worker){std::ifstream f((std::string(worker)+".log").c_str());return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());}
static void wait_log(const char* worker,const char* text){for(unsigned i=0;i<100 && log_text(worker).find(text)==std::string::npos;i++)pause_ms(10);assert(log_text(worker).find(text)!=std::string::npos);}

int main(int argc,char** argv){
    assert(argc==2);
    int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
    struct sockaddr_in dest;memset(&dest,0,sizeof dest);dest.sin_family=AF_INET;dest.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(sender,reinterpret_cast<struct sockaddr*>(&dest),sizeof dest)==0);
    socklen_t size=sizeof dest;assert(getsockname(sender,reinterpret_cast<struct sockaddr*>(&dest),&size)==0);
    unsigned port=ntohs(dest.sin_port);close(sender);
    sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;
    Model model(dispatcher,bundle,argv[1],port);
    assert(model.onInit());assert(model.posts==1);
    pin=0;model.testEvent(100,true);pause_ms(25);assert(log_text(argv[1]).empty());
    model.testEvent(100,false);pin=1;model.testEvent(100,true);
    wait_log(argv[1],"START");
    std::string started=log_text(argv[1]);
    assert(started.find("--session --diesel --n57-sgbd 010203")!=std::string::npos);
    assert(started.find("--signals 2048")!=std::string::npos);
    bd_frame sample;memset(&sample,0,sizeof sample);sample.sent_ms=bd_mono_ms();sample.valid=BD_BIT(BD_RPM);sample.value[BD_RPM]=800;
    char packet[BD_MAX_PACKET];int n=bd_encode(&sample,packet,sizeof packet);assert(n>0);
    assert(sendto(sender,packet,n,0,reinterpret_cast<struct sockaddr*>(&dest),sizeof dest)==n);
    pause_ms(5);model.testTick();assert(model.packets==1 && model.live);
    sample.demo=1;n=bd_encode(&sample,packet,sizeof packet);sendto(sender,packet,n,0,reinterpret_cast<struct sockaddr*>(&dest),sizeof dest);
    pause_ms(5);model.testTick();assert(model.packets==1);
    model.graph(true);model.testEvent(100,false);pause_ms(25);assert(log_text(argv[1]).find("STOP")==std::string::npos);
    model.graph(false);wait_log(argv[1],"STOP");model.testTick();assert(!model.live);
    model.onFinalize();
    int probe=socket(AF_INET,SOCK_DGRAM,0);assert(probe>=0);
    assert(bind(probe,reinterpret_cast<struct sockaddr*>(&dest),sizeof dest)==0);close(probe);close(sender);
    puts("N57 dashboard lifecycle: no unpinned launch, explicit pin args, accepted IPC, demo rejection, multi-view visibility and stop/close OK");
}
